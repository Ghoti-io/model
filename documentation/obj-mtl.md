# The OBJ and MTL dialect

**Status:** Specification of what this library accepts and produces, with
the intended behaviour stated where the implementation departs from it.
Departures are listed in section 11 with reproductions; each is a test to
write and then a fix to make.

Wavefront OBJ has no normative specification. The references are Wavefront's
own 1990s documentation as preserved at
[paulbourke.net/dataformats/obj](https://paulbourke.net/dataformats/obj/)
and [paulbourke.net/dataformats/mtl](https://paulbourke.net/dataformats/mtl/),
plus the behaviour of the readers in common use, which disagree with each
other and with the documentation. This document is therefore the
specification: where the references are silent or contradicted by practice,
it says what *this* library does and why.

---

## 1. Principles

**The parser records what the file says; the consumer validates.** A face
that names a vertex which does not exist, a face with two vertices, a
coordinate that is `nan` - none of these are reasons to reject a file, because
readers differ on how to treat them and a renderer can skip a bad face while
drawing the other ten thousand. The parser resolves and records; the consumer
checks ranges before indexing. cjelly's mesh builder is the reference consumer
and does exactly this, counting what it dropped.

**Malformed is different from unexpected.** A `v` line with two numbers is
malformed and is `GMDL_ERR_FORMAT` - which is stricter than either reference,
both of which pad the missing `z` with zero, and is the specification's
reading rather than a guess (3.1, 12). A `curv` line is a directive this library
does not implement and is ignored, as the OBJ documentation asks. A `Kd xyz`
line is a documented form this library does not implement and is
`GMDL_ERR_UNSUPPORTED`. The three answers are different because the caller's
next step is different.

**Nothing is truncated.** A line longer than the cap is `GMDL_ERR_LIMIT`, not
a prefix. A name longer than its field is `GMDL_ERR_LIMIT`, not the first 127
bytes. A prefix that parses is worse than an error because it parses as
something else.

**Limits are promises.** Every unbounded quantity has a cap in `GMDL_Limits`,
and the fuzzers drive every cap to check the promise holds.

---

## 2. Lines

Both formats are line-oriented text. The rules here apply to both.

### 2.1 Bytes and encoding

Input is bytes. Directives and numbers are ASCII. Names (`g`, `o`, `usemtl`,
`newmtl`, `mtllib`) are copied byte-for-byte and never interpreted, so a
UTF-8 name survives and so does a Latin-1 one.

A UTF-8 byte-order mark at the start of the input is skipped.

### 2.2 Line endings

A line ends at `\n`, at `\r`, or at `\r\n`, which counts as one ending. Mixed
endings in one file are fine. The final line need not be terminated.

### 2.3 Length

A line may be at most `max_line_length` bytes, not counting its ending. A
longer line is `GMDL_ERR_LIMIT` and parsing stops. A line of exactly the cap
is accepted.

### 2.4 Comments

`#` begins a comment that runs to the end of the line, anywhere on the line.
A line that is blank, or only a comment, is ignored. The comment is removed
before the continuation in 2.6 is looked for; that ordering is a decision the
format does not make for us, and 2.6 says why it was made this way.

### 2.5 Whitespace

Space and tab separate tokens. Any run of either is one separator. Leading
whitespace before the directive is permitted and ignored. Commas are
not separators: `v 1,2,3` is malformed.

### 2.6 Continuation

A `\` as the last non-blank character joins the next line to this one.
The joined line is subject to `max_line_length` as a whole.

**The comment is cut first.** A `\` that ends a comment therefore does not
continue anything, and a comment on a continued line still disappears. The
format does not settle this: the Wavefront specification uses the
continuation in its `bmat` and `surf` examples but never states the rule -
the words "continuation" and "joined" do not appear in it - and says only
that comments "can appear anywhere ... they are not processed". Neither
document mentions the two meeting.

Implementations split on it, so this is a choice and not a reading. Blender
4.3 joins first, so `# note \` swallows the line after it and the geometry
there is lost. VTK 9.3 does not join at all outside a few statement parsers,
so it keeps that line - and agrees with us here. We cut the comment first
because the alternative loses data over a backslash someone typed in prose,
and because a comment is by definition not processed.

The same split decides what happens after a name, where the two oracles
swap sides; `notes/model/obj-differential.md` in the workspace has the
measurements.

### 2.7 Directives

The first token on a line is the directive. Matching is exact and
case-sensitive, and the directive must be followed by whitespace or the end
of the line: `V` is not `v`, and `vertex` is not `v` followed by `ertex`.

A directive this library does not implement is ignored with its whole line.
**3.14 is the list**, and this section deliberately does not repeat it: the
set was written out in both places, the copy here fell behind, and for three
commits it described `s`, `l` and `p` as ignored while the parser was reading
them. One list, in the section whose subject it is.

**Numbers do not depend on the caller's locale.** A decimal point is `.` in
every OBJ and MTL file, whatever `LC_NUMERIC` says in the program that linked
this library. `strtof`, `sscanf` and `printf` all consult it, so the parser
pins `LC_NUMERIC` to C for the length of a load and the writer does the same
for a dump. Without that, a caller running under a comma locale reads
`v 0.5 0.5 0.5` as three zeroes and writes it back as `v 0,5 0,5 0,5`, which
no reader accepts.

A platform with no per-thread locale **fails to build** rather than producing
a library that misparses. The pin needs `uselocale`/`newlocale`; where those
are absent - MSVC has no `LC_NUMERIC_MASK`, and this library claims Windows -
the alternative was stubs that silently did nothing, which is a correctness
bug shipped quietly on a platform we cannot test.

Defining `GMDL_ALLOW_PROCESS_WIDE_LOCALE` pins `LC_NUMERIC` with `setlocale`
instead. Numbers are then read and written correctly, and the cost is the
promise `uselocale` buys: another thread formatting output during a load or
dump sees the C separator. That is a real trade rather than a defeat, which
is why it is offered - and why it is not the default.

---

## 3. OBJ

### 3.1 `v x y z [w]` and `v x y z r g b`

A vertex. Three numbers are required; fewer is `GMDL_ERR_FORMAT`. Text after
the numbers is ignored.

**Six or more numbers is the vertex-colour extension**, and the colour is
fields four to six. The rule is a count of *numbers*, not of tokens, so
`v 1 2 3 red green blue` is an uncoloured vertex and not an error.

| Numbers | Read as |
| --- | --- |
| 3 | `x y z` |
| 4 | `x y z w`; `w` is discarded (see below) |
| 5 | `x y z` and two numbers that are not a colour |
| 6 | `x y z r g b` |
| 7 or more | `x y z r g b`, the seventh onwards ignored |

Every row was measured against Blender 4.3.2, which agrees on all of them -
including the last, where `x y z w r g b` would be the other reasonable
reading and neither importer takes it. The boundary between five and six is
the whole rule, so it is what the tests pin.

`w` is a rational weight for free-form geometry, which section 3.14 does not
support, so nothing here could consume it and it is dropped.

Colours are recorded exactly as written: not clamped, not converted out of
whatever colour space the writer had in mind. Blender treats file values as
sRGB and hands its renderer `2.537` for a `1.5`, and discards every colour in
a file containing a negative component. Both are a consumer's decisions; a
parser that made them would leave no way back to what the file said.

A file may colour some vertices and not others. `GMDL_Obj.colors` is NULL
unless at least one `v` line carried a colour, and otherwise has exactly
`vertex_count` entries so that it is indexed by vertex number; the entries for
uncoloured vertices have `present` false and hold white, which is the value
that changes nothing when a consumer multiplies by it. Blender's answer to
such a file is to discard every colour in it - which a caller can still do
from what is recorded here, where the reverse is not true.

The writer emits a colour only for a vertex whose `present` is set, so a file
that mixed the two round-trips unchanged rather than gaining colours it never
had.

### 3.2 `vt u [v] [w]`

A texture coordinate. **Only `u` is required**; `v` and `w` default to zero
and `w` is discarded, there being nothing three-dimensional in this model's
texture space.

The two references disagree here - Blender reads `vt 0.5` and VTK calls it
"Error reading 'vt'" - so this follows the specification, which says both are
optional, and with it the more permissive of the two. `vt` with no number at
all is `GMDL_ERR_FORMAT`.

### 3.3 `vn x y z`

A normal. Three numbers required. Not normalised by the parser.

### 3.4 Numbers

A number is whatever `strtof` accepts: optional sign, decimal or exponent
form, and also `nan`, `inf` and hexadecimal floats. The parser records them
as written. A consumer that cannot draw a `nan` checks for it (section 8).

A token counts as a number when the conversion consumes *any* of it, so
`v 0 1 0abc` is three numbers. That is deliberately not the rule an MTL
option list uses (4.5), where a token must be a number in its entirety or the
arguments of `-o` would eat the front of the path that follows them. A `v` or
`vt` line has no path at the end, both reference importers read a partial
token this way, and 3.1 has said text after the numbers is ignored since
before colours arrived.

### 3.5 `f` - faces

```
f v v v ...
f v/vt v/vt v/vt ...
f v//vn v//vn v//vn ...
f v/vt/vn v/vt/vn v/vt/vn ...
```

Each token references a vertex and optionally a texture coordinate and a
normal, by index. The four forms may be mixed within one face, though no
writer does that. A fourth `/`-separated field is ignored.

**Indices.** In the file an index is 1-based; a negative index is relative,
with `-1` naming the most recently declared element *of that kind at that
point in the file*. The parser resolves both to 0-based indices and records
those. Relative indices are resolved against the counts at the moment the
face is read, not the final totals, because the two differ when vertices are
declared after faces that reference earlier ones - which is the whole reason
relative indices exist.

A missing field (`v//vn`, or `v` alone) is recorded as `-1`. A field written
as `0`, which no valid file contains, is also recorded as `-1`.

An index that names an element that does not exist - positive past the end,
or negative past the beginning - is recorded as resolved, that is, as a value
outside `[0, count)`. It is not an error (section 1).

An index too large to represent is held outside that range rather than
allowed to wrap into it. `f 4294967297` resolves to 4294967296, whose low 32
bits are zero, so narrowing it would point the face at vertex 0 - an index
the consumer's range check accepts and the file never named. Section 1 makes
range checking the consumer's job, and that division only holds while an
unrepresentable index cannot arrive disguised as a valid one, so such an
index saturates instead. The saturating value is not part of the contract;
"not in `[0, count)`" is.

A token that does not begin with an integer - `a`, `/1`, `1.5` is `1` followed
by junk - is `GMDL_ERR_FORMAT`.

**Count.** A face has however many vertices the line lists. Fewer than three
is not an error at parse time (section 1); a consumer that needs polygons
checks `count`. The first four vertices are in the face's fixed arrays and
the rest in `overflow`, which is `NULL` when `count <= 4`. There is no upper
bound on a face's vertex count other than `max_face_indices`.

**Material.** Each face records the material in force when it was read
(3.7), or `-1`.

### 3.6 `g [name]` and `o [name]`

Both begin a group: a named run of faces extending to the next `g` or `o` or
to the end of the file. They behave identically here, and
`GMDL_Obj_Group.is_object` records which of the two the file said, so the
dump writes the spelling back. They are not interchangeable to the tools that
write them - Blender makes an *object* of an `o` and a *vertex group* of a
`g` - and a reader that flattened them could not put the distinction back.

**The whole line is the name**, spaces included, as for `mtllib` and `usemtl`
(3.7, 3.8). The documentation permits `g a b c` to put the following faces in
three groups at once, and neither reference implements that: Blender reads
the line as one group called `a b c`. Taking only the first token was wrong
under both readings - it renames the group under the reference's and discards
two names under the documentation's - and the whole-line reading is the one
that keeps every byte, so a model that one day supports several names per
line can still recover them by splitting (12).

A bare `g` names the group `default`, as the documentation specifies.

A group's `start_face` and `face_count` describe a contiguous range. Faces
before the first group belong to no group. A group with no faces is retained
with `face_count` 0.

### 3.7 `usemtl name`

Sets the material for the faces that follow. The name is required; a bare
`usemtl` is `GMDL_ERR_FORMAT`. **The whole line is the name**, spaces
included, with blanks at either end dropped - the same reading `newmtl` uses
(4.1), because a name truncated on one side and not the other would stop
matching. Blender reads `usemtl two words` as one name; it substitutes
underscores when it writes, so such a file comes from some other exporter.

This is not the reading `g` uses, and the difference is deliberate: `g a b`
is documented as putting an element in two groups at once, so taking a group
line whole would settle that open question by accident (3.12, 12).

Materials are not resolved by the OBJ parser. Each distinct name is assigned
an index in order of first use, recorded in `material_mappings`, and faces
carry that index. A repeated name reuses its index. The consumer resolves
names against an MTL library with `gmdl_mtl_find()`.

### 3.8 `mtllib path`

Names the material library. This library keeps the **whole of the last
`mtllib` line** as one path, blanks at either end dropped. A bare `mtllib`
clears it.

The documentation allows several paths on one line, and Blender does not
implement that: given `mtllib a.mtl b.mtl` it looks for a single file called
`a.mtl b.mtl`, finds nothing, and loads no materials at all. So the
whole-line reading is the reference's as well as this one's, and it is what
the reference *writes*: exporting a document saved as `my model.obj` produces
`mtllib my model.mtl`, with no unusual settings involved. Taking the first
token of that line named the file `my`.

Several `mtllib` **lines** are a different matter - Blender loads all of
them, and this library still keeps only the last. That one is unresolved and
is in section 12, because it needs a list where the model has a fixed field.

### 3.9 Names and paths

Group, object and material names are at most `GMDL_OBJ_MAX_NAME_LENGTH - 1`
(127) bytes; the `mtllib` path at most `GMDL_OBJ_MAX_PATH_LENGTH - 1` (255).
A longer one is `GMDL_ERR_LIMIT`.

### 3.10 The result

`GMDL_Obj` holds ten arrays, each `NULL` when its count is zero:

| Array | Element | Notes |
| --- | --- | --- |
| `vertices` | `x y z` floats | in file order |
| `colors` | `r g b` and `present` | NULL unless some `v` carried a colour; otherwise one per vertex (3.1) |
| `texcoords` | `u v` | |
| `normals` | `x y z` | |
| `faces` | `GMDL_Obj_Face` | 0-based indices, `-1` for absent; `overflow` past four; `material_index`; `smoothing_group` |
| `lines` | `start`, `count`, `material_index` | one entry per `l` statement |
| `line_vertices` | vertex and texcoord index | every polyline's references, in file order |
| `points` | vertex and `material_index` | one entry per index any `p` named |
| `groups` | name, `start_face`, `face_count`, `is_object` | in file order; ranges are contiguous and do not overlap; `is_object` is the `o`/`g` spelling (3.6) |
| `material_mappings` | name, `index` | `index` equals position |
| `statements` | kind and text | `call` and `csh`, recorded and never run (3.13) |

plus `mtllib` and the allocator that owns it all. Everything is freed by
`gmdl_obj_free()`, including every face's `overflow`.

Groups cover faces only. A polyline or a point is not in any group, because
`g` in this model names a contiguous run of faces and nothing else.

### 3.11 `s` - smoothing groups

`s n` sets the smoothing group; `s 0` and `s off` both clear it, and zero is
the state a file starts in. Like `usemtl` it is state, applying to every face
after it until the next one, and it is recorded per face in
`GMDL_Obj_Face.smoothing_group`.

Per face rather than as a run of faces, because "do these two faces share a
smoothing group" is the question a consumer generating normals actually asks,
and a run would make it work that out for itself. A value that is neither
`off` nor a number is `GMDL_ERR_FORMAT`, and one that is a number too large
for an `int32_t` is `GMDL_ERR_LIMIT` - not the nearest value that fits.

Refusing rather than clamping, and the reason is the one 3.9 gives about a
truncated name. Every `int32_t` is a legitimate smoothing group, so there is
nowhere safe for an out-of-range one to land: clamped to `INT32_MAX` it
becomes a group that other faces may really be in, and the question this
field exists to answer - do these two faces share a group - gets a confident
wrong answer. That is the opposite of the call 3.5 makes for a face index,
where saturating is right *because* it keeps the value outside the range the
consumer checks. The two are not inconsistent; they are the same rule applied
to fields with different notions of "out of range".

Text after the number is still ignored, so `s 4abc` is 4. The change here was
the overflow, not a new strictness about what may follow.

The dump writes `s` only where the value changes, so a model that never
mentions smoothing writes none. The group in force carries across the runs
the dumper emits - the faces before the first `g`, then each group - which it
has to: a group whose faces turn smoothing *off* after one that had it on
says nothing at all if the writer assumes each run starts at zero, and the
reload then smooths faces the source did not.

### 3.12 `l` and `p` - polylines and points

`l v1 v2 ...` is one polyline of any length; each reference is `v` or `v/vt`.
The references live in one flat `line_vertices` array and each `GMDL_Obj_Line`
names its span, the same shape `groups` uses over faces. A face keeps its
first four vertices inside the element because nearly every face is a
triangle or a quad; a polyline has no typical length, so there is nothing to
special-case.

The token grammar is the face grammar of 3.5, so `1//2` on an `l` line
parses - the format does not give a line a normal, but files write one and
every other reader accepts it. The normal is read and dropped.

`p v1 v2 ...` declares one point per index. The statement boundary carries
no meaning that survives parsing, so `points` is a flat array of vertex
indices and the dump writes one `p` per point. A `p` reference is a bare
index: `p 1/1` is `GMDL_ERR_FORMAT`, because a point has no texture
coordinate to give.

Both resolve negative and relative indices exactly as faces do (3.5), and an
`l` or `p` naming nothing is `GMDL_ERR_FORMAT`.

`usemtl` reaches all three element kinds, so `GMDL_Obj_Line` and
`GMDL_Obj_Point` carry a `material_index` exactly as a face does, `-1` when
none was named. The dump writes the polylines and the points after the faces
and emits `usemtl` wherever the material changes, carrying what the faces
left rather than starting again - which is what keeps a material stated
between the faces and the polylines from being written twice.

### 3.13 `call` and `csh` - recorded, never executed

The two statements that ask the parser to *do* something rather than describe
geometry. `call filename [args]` pulls in another `.obj`; `csh command` runs a
shell command, and `csh -command` runs one whose exit status is ignored.

**This library does neither.** Both are recorded in `statements` as a
`GMDL_Obj_Statement` - a kind and the text after the directive, trailing
blanks removed, exactly as written. Nothing is split, resolved, opened or
run. The `-` on a `csh` is kept because the file wrote it; a `call`'s
filename and arguments stay together for the same reason.

Executing them is not a feature this library will grow, and the reason is not
that the act is dangerous but that it is **the caller's to take**. A flag
would be set by the consumer, while what actually runs is chosen by whoever
wrote the `.obj`; someone who enables it for their own generated assets and
later parses a downloaded model has handed that file a shell. Nothing about
the flag's scope would tell them those two paths had met. And there is no
capability in it: a pipeline that wants geometry from a command can run the
command and parse the result, which puts the decision where the context is.

So a consumer that *does* want to act on one gets the text and the
responsibility together. Section 8 says how to treat a texture map path; a
`call` is that with the stakes raised, because the file it names would then
be parsed, and a `csh` is a command.

A `call` or `csh` with nothing after it is `GMDL_ERR_FORMAT`.

The dump writes the statements first, in file order. Their position relative
to the geometry is not recorded - nothing else in this model is ordered
against the geometry either - and since this library never acts on them,
where they sit is not something a consumer of it can observe.

### 3.14 Not read

Two groups of directives, both deliberate.

**The free-form geometry sub-language**: `vp`, `cstype`, `deg`, `bmat`,
`step`, `curv`, `curv2`, `surf`, `parm`, `trim`, `hole`, `scrv`, `sp`, `end`
and `con`. These describe curves and surfaces - NURBS and their trimming -
which is a different kind of geometry from the polygon mesh this library
holds, not another record to append to it. Supporting them means a second
data model, not a field.

**Render attributes**: `bevel`, `c_interp`, `d_interp`, `lod`, `shadow_obj`,
`trace_obj`, `ctech`, `stech` and `mg`. These are state for a renderer and
change no geometry. They could be recorded, and the reason not to is that
there is nowhere honest to put them: they are per-state like `usemtl`, so
each would become a field on every face, describing something no consumer of
this library asks about. `shadow_obj` and `trace_obj` are the two carrying
real data - paths - and section 12 keeps the question open.

**Texture map libraries**: `maplib` and `usemap`. These stand to texture maps
as `mtllib` and `usemtl` do to materials, and almost nothing writes them -
exporters put the map in the `.mtl` instead (4.5). Reading them means a
second name-to-index mapping beside the material one, for a feature with no
observed users.

A line whose directive is none of the above and none of 3.1-3.13 is skipped,
which is how a file carrying an exporter's private extension still loads.

---

## 4. MTL

### 4.1 `newmtl name`

Begins a material. The name is required (`GMDL_ERR_FORMAT` without one) and
is **the whole of the line**, spaces included, with blanks at either end
dropped - the same reading `usemtl` uses (3.7) and the same one a texture map
path already used (4.5). Blender reads `newmtl two words` as one material.
Duplicate names are retained as separate materials; `gmdl_mtl_find()` returns
the first.

The two sides have to agree about where a name ends or an OBJ stops finding
its own materials, and while both truncated at the first blank they agreed
with each other and nothing inside this library could see it.

Properties before any `newmtl` have nothing to apply to and are ignored.

### 4.2 Properties

Each applies to the current material. Trailing text after the expected
values is ignored. Every property is optional; an absent one leaves zero,
**except `d`, which is 1**.

`d` is the exception because its zero is not a neutral starting value: it
means the material is invisible, and a file says that by writing `d 0`, not
by saying nothing. Leaving it at zero made an ordinary material disappear in
any consumer that believed the field. An explicit `d 0` is still zero.

Those defaults exist so that a consumer can read a field without thinking.
They are not what the writer uses. **`present` records which properties the
source actually stated**, as a bitwise OR of ::GMDL_Mtl_Present, and
`gmdl_mtl_dump()` writes only those - see 9. The two are separate questions
and conflating them is what made the writer assert things no file said.

`Kd` is the property where it shows. An absent `Kd` is a light default -
white in VTK 9.3, 0.8 grey in Blender 4.3 - while `Kd 0 0 0` is black, so a
dump that wrote every field turned a material that never mentioned `Kd` into
a black one. `Ka`, `Ks` and `Ns` happen to read the same absent as zero in
both, and neither honours `illum` at all, but the dumper does not rely on
that: it writes what was stated and nothing else.

| Property | Values | Field |
| --- | --- | --- |
| `Ka r g b` | ambient colour | `Ka` |
| `Kd r g b` | diffuse colour | `Kd` |
| `Ks r g b` | specular colour | `Ks` |
| `Ke r g b` | emissive colour | `Ke` |
| `Tf r g b` | transmission filter | `Tf` |
| `Ns n` | specular exponent | `Ns` |
| `Ni n` | optical density (index of refraction) | `Ni` |
| `d n` | dissolve; 1 is opaque, and is the default | `d` |
| `Tr n` | transparency; **not** folded into `d` - see below | `Tr` |
| `illum n` | illumination model, integer | `illum` |
| `sharpness n` | reflection sharpness, integer | `sharpness` |
| `Pr n` | PBR roughness | `Pr` |
| `Pm n` | PBR metallic | `Pm` |
| `Ps n` | PBR sheen | `Ps` |
| `Pc n` | PBR clearcoat thickness | `Pc` |
| `Pcr n` | PBR clearcoat roughness | `Pcr` |
| `aniso n` | PBR anisotropy | `aniso` |
| `anisor n` | PBR anisotropy rotation | `anisor` |
| `map_aat on\|off` | texture antialiasing hint | `map_aat` |

**`Tr` is recorded and never folded into `d`.** The format describes it as
`1 - d`, which makes deriving one from the other look obvious, and measurement
says otherwise: Blender 4.3 ignores `Tr` outright, VTK 9.3 does too, and both
take `d` whichever order the two appear in. A file stating `d 0.75` and
`Tr 0.9` is stating two things that cannot both describe one surface, and it
is not a parser's place to pick. Both are kept, `present` says which the file
gave, and a consumer that wants the relationship can apply it knowing that.

The colour properties have three documented forms. `K? r g b` is the
ordinary one. `K? r` - one value, meaning `r r r` - is accepted and expanded.
`K? xyz ...` (CIE XYZ) and `K? spectral file [factor]` are
`GMDL_ERR_UNSUPPORTED`. `d -halo n` is `GMDL_ERR_UNSUPPORTED`.

A property whose values do not parse - `Kd 0.5 x`, `illum x` - is
`GMDL_ERR_FORMAT`.

### 4.3 Ignored

Nothing the format defines. Every directive in the reference document is read
into a field: the properties in 4.2, the texture maps in 4.5, the reflection
maps in 4.6.

A line whose directive is not one of them is skipped, which is how a file
carrying a renderer's private extension still loads.

### 4.4 The result

`GMDL_Mtl` holds `materials`, `material_count` and the allocator.
`gmdl_mtl_find(mtl, name)` is a linear search by exact name.

Each `GMDL_Mtl_Material` carries a usable value in every property field and
a `present` mask saying which of them the file stated. A renderer may ignore
`present`; anything that writes a material out must not.

The texture map paths need no bit in the mask, because a pointer answers the
question by itself: NULL means the file stated none, and no file can ask for
NULL. That covers the maps in 4.5 and every `refl` slot in 4.6. They belong to
the `GMDL_Mtl` and are freed with it, so a path that has to outlive the
library must be copied out.

### 4.5 Texture maps

| Directive | Field |
| --- | --- |
| `map_Ka path` | `map_Ka` |
| `map_Kd path` | `map_Kd` |
| `map_Ks path` | `map_Ks` |
| `map_Ke path` | `map_Ke` |
| `map_Ns path` | `map_Ns` |
| `map_d path` | `map_d` |
| `map_bump path`, `bump path`, `map_Bump path` | `map_bump` |
| `map_Pr path` | `map_Pr` |
| `map_Pm path` | `map_Pm` |
| `map_Ps path` | `map_Ps` |
| `norm path` | `norm` |
| `disp path` | `disp` |
| `decal path` | `decal` |

`refl` is a map too, and has a shape of its own; it is 4.6.

`bump`, `map_bump` and `map_Bump` are three spellings of one property and
share a field; `refl` and `map_refl` (4.6) likewise. The dump writes
`map_bump` and `refl`, so the round trip is of the material rather than of
the keyword that set it.

**These are extra spellings, not a relaxation of 2.7's exact matching.**
Blender 4.3.2 accepts `map_Bump` and `map_refl` while ignoring `kd`, `KD`,
`map_kd`, `map_BUMP`, `Map_Bump` and `map_Refl` - it carries specific aliases
rather than folding case. Matching case-insensitively would make this library
accept input the reference rejects, which is a worse disagreement than the
one it would fix, so the accepted set is exactly the measured one and a test
pins the rejected spellings too.

**Options come first, then the path.** Each option is introduced by a leading
`-`:

| Option | Field | Default |
| --- | --- | --- |
| `-blendu on\|off` | `blendu` | on |
| `-blendv on\|off` | `blendv` | on |
| `-clamp on\|off` | `clamp` | off |
| `-boost f` | `boost` | 0 |
| `-bm f` | `bm` | 1 |
| `-mm base gain` | `mm[2]` | 0, 1 |
| `-o u [v [w]]` | `o[3]` | 0, 0, 0 |
| `-s u [v [w]]` | `s[3]` | 1, 1, 1 |
| `-t u [v [w]]` | `t[3]` | 0, 0, 0 |
| `-texres n` | `texres` | 0 |
| `-imfchan r\|g\|b\|m\|l\|z` | `imfchan` | `l` |
| `-type <name>` | `type`, and the `refl` slot (4.6) | untyped |

Each field holds its default whether or not the file said so, and the map's
own `present` mask says which were stated - the same division as 4.2, for the
same reason.

`-texres` is the only option whose field is an integer, so it is the only one
that can be handed a number it cannot hold. One outside `int32_t` - `1e30`,
or a `nan` - is `GMDL_ERR_LIMIT` (5). Converting it instead was undefined
behaviour, reachable from an ordinary map line, and it went unnoticed because
GCC leaves `float-cast-overflow` out of `-fsanitize=undefined` *and* out of
what `-fno-sanitize-recover=undefined` covers, so the sanitiser gate was
neither checking this class nor able to fail over it. Both halves are named
explicitly in the Makefile now.

**These were `GMDL_ERR_UNSUPPORTED` until the `map_Bump` alias was added, and
that combination was untenable.** Blender writes `map_Bump -bm 0.350000
nrm.png` for every normal or bump map it exports. While `map_Bump` went
unrecognised the line was skipped and the file loaded without its bump map;
recognising the spelling without the option turned that into a refusal of the
whole file. The first is silent data loss, the second is worse, and
implementing the options is the only answer that is neither.

An option this library does not know is still `GMDL_ERR_UNSUPPORTED`. Several
of the documented ones change what a map *means* - `-clamp` and `-imfchan`
among them - so handing a consumer the path while dropping an option it could
not read would describe a material the file did not.

**A vector option takes only as many components as are really numbers.**
`map_Kd -o 1 2 2.png` is an origin of `(1, 2, 0)` and a path of `2.png`: a
token counts as a component only when the conversion consumes all of it, so
the filename cannot be eaten a digit at a time.

**The path is the whole of the rest of the line** once the options are
consumed, with trailing blanks removed, so `map_Kd my tex.png` names one file
called `my tex.png`. The
format's own description does not say; both Blender 4.3 and VTK 9.3 read it
this way, and that agreement is the only reason to prefer it over taking the
first token. Trailing blanks are dropped because Blender drops them and VTK
keeps them and then cannot find the file it just named.

The path is stored exactly as written. No separator is translated and
nothing is resolved against the `.mtl`'s own directory, because only the
caller knows where the file it handed over came from. A repeated directive
keeps the last one; the format has no way to say two maps of one kind.

Unlike the names in 3.9 there is no fixed cap: the path is allocated, so
`max_line_length` is what bounds it. It is freed with the ::GMDL_Mtl.

**A line whose argument begins with `-` carries texture options** -
`-o`, `-s`, `-clamp`, `-bm` and the rest - and is `GMDL_ERR_UNSUPPORTED`.
The file is well-formed and this library is the one falling short, which is
the distinction section 1 draws. Refusing rather than guessing is deliberate,
and measured: the two references do not agree on what the options are.
Blender knows `-clamp` and consumes it; VTK 9.3 does not, and folds it into
the filename, so `map_Kd -clamp on t.png` names `t.png` in one and
`-clamp on t.png` in the other. Silently dropping the options would be worse
than either, because `-s 2 2 2` is a scale a renderer would then not apply:
a wrong picture rather than a missing one.

A directive with no path at all is `GMDL_ERR_FORMAT`, for the same reason
`Kd 0.5 x` is. Both references instead ignore the line; this is a place where
the library is deliberately stricter than both.

### 4.6 `refl` - reflection maps

A reflection map names the surface it covers, so unlike every other map
directive its argument may begin with an option:

```
refl -type sphere      chrome.png
refl -type cube_top    top.png
refl -type cube_bottom bottom.png
refl -type cube_front  front.png
refl -type cube_back   back.png
refl -type cube_left   left.png
refl -type cube_right  right.png
```

`material->refl[]` is indexed by ::GMDL_Mtl_Refl_Type, one slot per surface,
each NULL until stated. A cube map is six separate lines, which is the reason
for an array: it is the one property in MTL that a file states across several
directives.

`-type` is what chooses the slot, which is why it is the one option with a
consequence beyond being recorded. A `-type` naming none of the seven is
`GMDL_ERR_FORMAT`, on `refl` and anywhere else.

**Anywhere else it is recorded and does nothing**, in `GMDL_Mtl_Map.type`
with `GMDL_MTL_MAP_HAS_TYPE` in the mask. It used to refuse the file, which
was left over from when every option did: `-bm` on a colour map is exactly as
meaningless and has been read since 4.5 stopped refusing, so the two were
inconsistent and the inconsistency cost a caller the whole library over an
option that could simply be written down. Blender keeps the texture from such
a line; this keeps the texture and the option.

The paragraph this replaced said "any other option is still
`GMDL_ERR_UNSUPPORTED`", which had been true and stopped being true in the
same commit that made 4.5's table - a reminder that a section describing a
neighbouring section's behaviour goes stale without anything failing.

A `refl` with no `-type` at all goes to ::GMDL_MTL_REFL_UNTYPED and is written
back without one. The format says the option is required; both references
accept the line anyway, so refusing it would reject files that exist, and
assuming a surface would record something the file never said.

---

## 5. Limits

`GMDL_Limits`, with `gmdl_limits_default()`:

| Field | Default | Counts |
| --- | --- | --- |
| `max_line_length` | 65536 | bytes in one line, excluding its ending |
| `max_vertices` | 0 (unlimited) | `v` records |
| `max_texcoords` | 0 | `vt` |
| `max_normals` | 0 | `vn` |
| `max_faces` | 0 | `f`, `l` and `p` elements **together**, one budget |
| `max_face_indices` | 0 | vertices in one face, or in one `l` |
| `max_groups` | 0 | `g` and `o` together |
| `max_materials` | 0 | distinct `usemtl` names in OBJ; `newmtl` in MTL |
| `max_statements` | 0 | `call` and `csh` records |

`0` means no limit. When a record would take a count from `limit` to
`limit + 1`, the result is `GMDL_ERR_LIMIT` and parsing stops.

`max_faces` is one budget across three arrays, not one each. It was three
separate checks against the same field until it was measured, so a file with
one `f`, one `l` and one `p` loaded under a cap of two - a caller bounding
memory from untrusted input was getting three times the bound they set, on
the one axis the field exists for. The whole suite passed either way, because
every case exercised one element kind at a time: a limit spanning several
arrays needs a case that spans them. A `p` statement costs one per index it
names rather than one per line, because that is what the model stores
(3.10). `NULL` limits
mean the defaults. Only the line cap has a default because the input's size
already bounds the record counts, and a legitimate model can be very large;
set the others for untrusted input.

Not every bound is in this table. A field's own width is one too, and three
places read an integer with `sscanf("%d")`, which is undefined behaviour when
the value does not fit - C17 7.21.6.2p10 - and which glibc resolved by
wrapping: `s 2147483648` arrived as `-2147483648`, and
`s 99999999999999999999` as `-1`. `s`, `illum` and `sharpness` now answer
`GMDL_ERR_LIMIT`, and a `-texres` whose number is outside `int32_t` does too
(4.5). The same reasoning as a record cap: a value the model cannot hold is a
limit, and a limit is reported rather than resolved by storing something
else.

**A cap is measured by what it bounds, not by what it returns.** A parser
that read a whole file into memory and then refused it answers
`GMDL_ERR_LIMIT` exactly as one that stopped at the cap does - same status,
opposite memory behaviour, and memory is what a caller setting these fields
is bounding. Every test keyed on the result code is therefore measuring the
half that does not differ. Two tests measure the other half with a recording
allocator: the high-water mark does not move when the input quadruples under
unchanged caps, and one enormous element does not build itself before being
refused. The second was seen to fail against a version that answered
`GMDL_ERR_LIMIT` for exactly the same files and allocated the whole face
first; every status-keyed test passed against it.

Section 1 promises a cap on every unbounded quantity, and `max_statements`
was missing from this table until it was measured for: with every other field
set, a file of nothing but `call` lines was still accepted without bound. The
fuzzers cannot find that class of gap, because what they drive is the set of
caps that *exist* - a quantity with no field is invisible to them. What
catches the next one is a test that walks `GMDL_Limits` field by field and
requires each to refuse something, which fails if a field is added without
enforcement.

---

## 6. Results

| Result | When |
| --- | --- |
| `GMDL_OK` | parsed; `*out` is set |
| `GMDL_ERR_INVALID` | `out` or `stream` is `NULL` (`path` for `_file`) |
| `GMDL_ERR_IO` | `_file` could not open or read the path, for any reason including a path too long for the filesystem |
| `GMDL_ERR_FORMAT` | a line was malformed (sections 3 and 4) |
| `GMDL_ERR_UNSUPPORTED` | a documented form this library does not implement (4.2) |
| `GMDL_ERR_LIMIT` | a `GMDL_Limits` cap was exceeded, or a name or path was too long |
| `GMDL_ERR_OOM` | the allocator returned `NULL`, or the system had no memory to open the file |

On any failure `*out` is `NULL` and nothing is allocated for the caller. The
file is read to the first error and no further; there is no partial result.

`GMDL_ERR_INVALID` means a caller argument is wrong and nothing else. cutil
tells a path the filesystem will not accept apart from one it could not read,
and that distinction is deliberate there; it is dropped here, because this
enumeration is the narrower one and a path that is too long on one mount and
fine on another is not the caller misusing the API.

An empty input, or one that is entirely comments and blank lines, is
`GMDL_OK` with every count zero.

---

## 7. Streams

Both parsers read through `GMDL_Stream`. A memory stream borrows the caller's
bytes for its lifetime. A file stream reads the whole file into memory at
creation - in chunks, so a path that names a pipe or a device works - and is
a memory stream from then on. This means a read error can only happen at
creation, where it is `GMDL_ERR_IO`.

The reading itself is `gcu_file_read()` from cutil rather than a local loop.
That is not only to delete a duplicate: cutil opens through the wide entry
point on Windows, where `fopen()` takes the path in the process code page and
therefore cannot name every file the filesystem accepts. A UTF-8 path that
does not survive that conversion opens a different file, or none at all, and
this library has no way to tell which happened.

`gmdl_stream_read_line()` returns one line without its ending, NUL-terminated,
in the caller's buffer. At end of stream it returns `GMDL_ERR_IO`, which is
the only way that result arises from a stream once it exists;
`gmdl_stream_eof()` says the same thing without consuming anything. A line
that will not fit in the buffer - `size - 1` bytes or fewer fit - is
`GMDL_ERR_LIMIT`, and the line is consumed, so the caller cannot retry with
a bigger buffer; the parsers treat it as fatal.

---

## 8. What a consumer must check

Because of section 1, a `GMDL_OK` model may contain:

- a face whose `count` is 0, 1 or 2;
- a face index outside `[0, count)` for its array, including `-1` in a
  `vertex` slot (the file wrote `0`);
- `nan` or `inf` coordinates;
- a group with no faces;
- a `material_index` whose name no MTL library defines;
- a texture map path naming anything at all;
- a `call` or `csh` statement naming any file or command at all (3.13).

The last two are the entries here that are security questions rather than
correctness ones, and the statements are the sharper of them: this library
records what they say and never acts on it, so a consumer that chooses to act
is choosing to run a command, or to parse a file, that its input named. A map path is a string from the file, unresolved and
unexamined - `../../etc/passwd` and an absolute path are both things a `.mtl`
can say, and this library will hand either back without comment, because a
parser that silently rewrote the path would be lying about what the file
contains. A consumer that opens one must resolve it against a directory it
chose and check the result is still inside it. Nothing downstream can do
that check, because by then the path looks like any other.

A consumer indexes nothing without a range check. cjelly's
`cjelly_model_mesh_from_obj()` skips a face on any of the first two and counts
it in `dropped_faces`; that is the pattern.

---

## 9. Dump

`gmdl_obj_dump()` and `gmdl_mtl_dump()` write a model back out as text. The
guarantee is **structural round-trip**: parsing the dump yields a model with
the same counts, the same indices, the same names, the same group ranges and
the same material assignments. Floats are written with `%.9g`, which
round-trips every float exactly, and with `LC_NUMERIC` pinned to C for the
length of the dump (2.7), so the bytes are the same whatever locale the
calling program is in.

Indices are written 1-based in a wider type than they are stored in. An
index of `INT32_MAX` is representable and recordable, and `INT32_MAX + 1` in
an `int` is undefined behaviour - so the addition is done in `long long`.

**The OBJ output is read correctly by other tools.** Blender 4.3 and VTK 9.3
were both given this library's dumps of the checked-in models and of
synthetic cases, and both produced geometry identical to what they read from
the sources - the `%.9g` exponent forms, `1.00000001e-07` and
`-3.40282347e+38` among them. That is a measured result rather than an
intention; `notes/model/obj-differential.md` in the workspace has the method.

**Vertex colours survive both directions**, measured as a loop rather than
as two half-checks: Blender was made to export a coloured mesh, this library
parsed that file and dumped it, and Blender read the dump with the same three
colours on the same three vertices. A colour this library invented would have
shown up as a colour Blender did not have, and a colour it dropped as none at
all; the loop catches both, where comparing our parse against our own dump
catches neither.

**The MTL output is read correctly too**, measured the same way. The dump
writes a property only when the material's `present` mask says the source
stated it, so a material that omitted `Kd` or `d` reads back from Blender
and VTK exactly as the source did, and one that stated `Kd 0 0 0` or `d 0`
keeps its black or its invisibility. Both halves of each pair were checked,
because a fix that got either backwards would look correct from the other
side.

**Texture map paths survive too**, measured the same way: a library holding
`map_Kd my tex.png`, `map_Ka` and `map_bump` was dumped, and Blender read the
dump exactly as it read the source - same files in the same slots, the space
in the filename included, the bump map wired to the same normal input. The
rest of the vocabulary was measured the same way once it existed: a material
carrying `Ke`, `Tf`, `Ni`, five PBR scalars, `map_Ke`, `map_Pr`, `map_Pm` and
a `refl -type sphere` read identically from the source and from the dump,
down to which socket each image landed on.

VTK 9.3 is not a second opinion on any of that. It reads `Ka`, `Kd`, `Ks`,
`Ns`, `d` and one texture, and ignores every directive added since - so for
the newer half of 4.2 and 4.5 there is one reference rather than two, and the
agreement recorded above is Blender's alone. An oracle that declines a whole
category is not a second reading of it.

VTK 9.3 agrees on the paths and disagrees about which one it uses, for a
reason worth stating because it is not a defect on either side. VTK keeps
**one** texture per material, which `map_Ka` and `map_Kd` both fill and the
**last** of them wins (`map_Ns` does not compete; a material with only one is
unaffected). This library stores a material rather than a list of
directives, so the dump writes the maps in a fixed order - and that order is
chosen so `map_Kd` is written after `map_Ka`, which makes VTK settle on the
diffuse map. A source that wrote them the other way round is therefore read
by VTK one way from the source and another from the dump. The dump is not
wrong; it cannot preserve an order it does not keep, and of the two answers
it lands on the one a single-texture renderer wants. A test pins the order
so it stays deliberate.

The dump writes `usemtl` when the material changes, `s` when the smoothing
group does, `g` for each group before its faces, and relative indices as
absolute ones.

Polylines and points are written in two passes, before the faces and after,
split on whether they name a material. OBJ can change the material in force
but cannot turn it off, so an element carrying none has to be written while
none is in force - and the faces, which come in between, may set one. A
polyline declared before a file's first `usemtl` otherwise came back carrying
the material of a face that followed it. Within one parse `material_index`
moves from -1 to a mapping and never back, so the elements naming none are a
prefix of each array and the split preserves each array's own order.

**Every write is checked, and every one of those checks is exercised.** Both
dumpers are mostly error handling by line count, and none of it had ever run:
a test that dumps to a file that works cannot reach a single one of those
arms. The tests hand over a stream that accepts a set number of writes and
refuses the rest, then sweep that number from zero upwards, so the failure
walks through the whole of a dump one position at a time. A model with groups
and one without are both swept, because they leave by different branches.
`GMDL_ERR_IO` is the only answer any position may give.

Four things a `GMDL_OK` model may hold cannot be written back, because the
format has no spelling for them rather than because the dumper is wrong: a
material no element uses, an index below -1, a name - or a texture map path -
ending in a backslash, and an element that names **no** material while one is
in force. Section 10 says what each one is and how the fuzzers account for
it.

That last one is not a state a parse can reach: `material_index` moves from
-1 to a mapping and never back, because OBJ can change the material in force
but cannot turn it off. A model built by hand can hold it, and the dump then
writes nothing, so the reload gives the element the previous material.

---

## 10. Fuzzing

`tests/fuzz/fuzz_obj.cpp` and `fuzz_mtl.cpp` take the first byte of the input
as an options byte and the rest as the file. Each bit set lowers one limit to
a small value, so the same corpus exercises every cap:

| Bit | Limit | Value |
| --- | --- | --- |
| `0x01` | `max_line_length` | 64 |
| `0x02` | `max_vertices` | 16 |
| `0x04` | `max_faces` | 16 |
| `0x08` | `max_face_indices` | 8 |
| `0x10` | `max_groups` | 4 |
| `0x20` | `max_materials` | 4 |

The invariants the harnesses check: whatever the result, the parser neither
crashes nor leaks; and a `GMDL_OK` model is dumped, parsed back, and compared
against the original. For OBJ the comparison covers every count, every
coordinate value, every vertex colour - its three components and whether the
vertex had one at all, since an absent colour holds white and a dumper that
wrote white for it would reload with all three components matching - every
group name and span, the `mtllib` path, each face's
material by name and smoothing group, each polyline's span and material,
each point's material, every recorded `call` and `csh`, and - with the
exception below - every face, polyline and point index. For MTL
it covers every property value, the `present` mask and every texture map
path - the `refl` slots included, and including whether one was stated at
all, since a NULL that comes back as a path is exactly what a dumper keying
on the wrong thing produces. The paths are gathered by a helper rather than
listed at the comparison, so a path added to the material cannot be left out
of the check and quietly narrow what the harness verifies. A dumper that
invents a property or drops one is caught as readily as one that gets a value
wrong.

That comparison used to be described here and not implemented: the harnesses
dumped to `/dev/null` and read nothing back, which is why a dumper that
dropped every face preceding the first group survived millions of
executions.

Four things are outside the comparison, because the format cannot express
them rather than because the dumper is wrong.

- **A material no element uses.** The dumper writes `usemtl` only where the
  material changes, so a mapping created by a `usemtl` line that no face,
  polyline or point follows is never written. `material_mapping_count` is
  therefore not stable; each element's material *is*, and is compared by
  name.
- **An element naming no material while one is in force.** No OBJ spelling
  exists, so nothing is written and the reload reads the previous material.
  A parse never produces it (section 9), so the fuzzers cannot reach it; it
  is listed because a hand-built model can.
- **A name, a texture map path, or a recorded statement's text, ending in a
  backslash.** A name is
  written last on its line, so one ending in `\` lands exactly where 2.6
  reads a continuation: re-reading `newmtl a\` joins the `Ka` line after it
  and yields the material `aKa`, and `g \` at end of file loses the
  backslash and becomes `default`. A map path is last on its line too, so
  `map_Kd a\` swallows whatever follows in the same way. Doubling the
  backslash only moves the continuation, so the format has no way to say it.
  Such a name or path reaches the parser only from a line ending in two
  backslashes, which no real file contains - the fuzzer finds it because it
  writes bytes rather than files. A model holding one is skipped entirely.
- **An index below -1.** An index of `k >= 0` is written as `k + 1` and
  an absent one as `0`, both of which read back as themselves. An index of
  `-2` or lower - which only a relative index reaching past the beginning of
  the file produces, and which 3.5 records rather than rejects - is written
  as a negative number, and OBJ reads a negative index as relative. There is
  no OBJ spelling for such an index, so a model containing one is exempt
  from the index comparison and from nothing else. Polylines and points
  resolve their indices the same way, so the exemption reaches them too.

All four exemptions were measured rather than assumed: over the accumulated
corpus they account for every disagreement, and the invariant as stated
holds on all of it.

**A map missing from two lists at once is missing symmetrically.** Every
texture map in `GMDL_Mtl_Material` is named in four places that must agree -
the free list, the defaults list, the dump list, and the fuzzer's comparison -
and nothing about the language makes them. Forget the free list and the path
leaks; forget the defaults and the map holds zeroes where 4.5 documents a
scale of 1; forget the dump list and the map is dropped on the way out - and
forget the comparison as well, which is the same oversight, and the round
trip agrees about a map that is gone. Neither list can check the other,
because each is half of the instrument.

The same shape is on the OBJ side with two lists rather than four: every
owned array in `GMDL_Obj` is named in the steal that moves it out of the
parser's builder and in the free. A missed steal hands the caller NULL for
records that parsed; a missed free leaks the lot.

`tools/check-lists.py` runs with `make test` and compares every list against
its struct. All four agreed when it was written, which is what a gate
is for: holding a state that is currently true rather than discovering one
that is not. It was seen to fail eleven ways - one per list, one
per struct for a field added and listed nowhere, and one per struct for the
gate's own pattern no longer matching, since a gate that cannot find what it
is checking passes in silence.

**What the fuzzers structurally cannot find.** They drive the caps that
exist, so a quantity with no field in `GMDL_Limits` is invisible to them -
`call` and `csh` allocated without bound for as long as `max_statements` was
missing, and no amount of fuzzing would have said so. They compare a load
against a reload, so a defect that is symmetric across both survives the
comparison: a face index that wrapped on the way in wrapped identically on
the way back and the invariant held while the value was wrong. The same
shape reaches *unit* tests written as round trips, and did: the vertex-colour
round trip compared the reload's colour count against the source's, so
removing the step that hands colours to the model left both at zero and the
test green. A round trip needs one assertion about what the source actually
held, or it can agree about nothing at all. And an input
large enough to show an unbounded allocation is far past the sizes libFuzzer
generates. These are jobs for unit tests that assert the property directly
(section 5), not for more fuzzing time.

---

## 11. Implementation status

Nothing known. Every defect this section has listed is fixed and pinned by a
test; the shortfalls that remain are absences rather than misbehaviour, and
section 12 is where they are written down.

---

## 12. Open questions

- **Several `mtllib` lines.** Not the question it used to be: "keep a list,
  or keep one path including spaces" turned out to be a false choice, because
  Blender does both - one path per line, spaces and all, and every line's
  library loaded. The spaces half is fixed (3.8). What is left is that this
  library keeps only the last line's path, so a file naming two libraries
  silently loses one. Fixing it needs `GMDL_Obj.mtllib` to become a list,
  which is a breaking change to a published field with a consumer in the
  workspace (`libs/cjelly`), so it is a decision rather than an oversight.
- **Multiple group names per `g` line.** The documentation allows `g a b`.
  Measured since: neither reference implements it - Blender reads the line as
  one group named `a b` - so the conflict with the contiguous range model is
  not one anybody is having in practice. 3.6 now keeps the whole line, which
  loses nothing either way and leaves this decidable later; what remains is
  whether to act on it.
- **Free-form geometry.** `curv`, `surf` and the rest of the sub-language in
  3.14. A second data model rather than more fields, so it is a decision
  about what this library is for.
- **`shadow_obj` and `trace_obj`.** The two render attributes carrying real
  data - a path each. Recording them means deciding where per-state
  attributes live, which 3.14 explains is the blocker for all nine.
- **A map directive with no path.** `GMDL_ERR_FORMAT` here, ignored by both
  references. Strictness is defensible and this is now the only place 4.5
  takes it further than either: `-type` on a colour map was the other, and it
  was an oversight rather than a position (4.6).
- **A `v` line with fewer than three numbers.** `GMDL_ERR_FORMAT` here;
  Blender and VTK both read `v 0 1` as `(0, 1, 0)`. The specification requires
  three, so this is the specification against both references - the opposite
  of the call made for `vt` in 3.2, where the specification said the numbers
  were optional and the strict reference was the one out on its own. Worth
  noting together, because "follow the specification" and "follow the
  references" pick different sides here and it is not obvious either is wrong.
- **Extra face fields.** Reject `1/2/3/4`, or keep ignoring it?
- **`nan` and `inf`.** Record faithfully (current) or reject at parse time?
  Section 1 argues for faithful; a stricter mode via `GMDL_Limits` is an
  option that changes no default.
- **Which exporters' spellings are still missing.** `map_Bump` and `map_refl`
  were found by asking Blender about 27 candidate spellings, not by reading
  the reference - the reference does not list them. Blender is one exporter;
  Maya, 3ds Max and Substance have their own habits, and the only honest way
  to know is to survey files they produce rather than to reason about what
  they ought to write. Until then the accepted set is "what was measured",
  which is a smaller claim than "what exists".
