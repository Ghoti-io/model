# OBJ and MTL

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
reading rather than a guess (3.1, 12). A directive this library does not
know is ignored with its whole line, as the OBJ documentation asks (3.14).
A map option this library does not know is a form the file stated and this
library does not implement, and is `GMDL_ERR_UNSUPPORTED` (4.5). The three
answers are different because the caller's next step is different.

**Nothing is truncated.** A line longer than the cap is `GMDL_ERR_LIMIT`, not
a prefix. A name longer than its field is `GMDL_ERR_LIMIT`, not the first 127
bytes. A prefix that parses is worse than an error because it parses as
something else.

**Limits are promises.** Every unbounded quantity has a cap in that format's
options struct,
and the fuzzers drive every cap to check the promise holds.

---

## 2. Lines

Both formats are line-oriented text. The rules here apply to both.

### 2.1 Bytes and encoding

Input is bytes. Directives and numbers are ASCII. Names (`g`, `o`, `usemtl`,
`newmtl`, `mtllib`) are copied byte-for-byte and never interpreted, so a
UTF-8 name survives and so does a Latin-1 one.

A UTF-8 byte-order mark at the start of the input is skipped.
`GMDL_Obj_Options.freecad` leaves it in place, so the first line does not
match a directive. `GMDL_Obj_Options.keep_byte_order_mark` does the same
thing and nothing else; Blender's measured file also sets
`omit_unresolved_faces`, because the face still names the hidden vertex.

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
`GMDL_Obj_Options.freecad` does not cut comments, so `#` on a face line is a
token and that face is omitted. `GMDL_Obj_Options.reject_face_comment` fails
the file instead, which is VTK 9.3. `GMDL_Obj_Options.join_before_comment`
looks for the continuation in 2.6 before this cut, so `# note \` swallows
the next line, which is Blender 4.3.

### 2.5 Whitespace

Space and tab separate tokens. Any run of either is one separator. Leading
whitespace before the directive is permitted and ignored.
`GMDL_Obj_Options.freecad` does not skip it, so a line that does not start
at column 0 is not a directive. Commas are not separators:
`v 1,2,3` is malformed.

### 2.6 Continuation

A `\` as the last non-blank character joins the next line to this one.
The joined line is subject to `max_line_length` as a whole.
`GMDL_Obj_Options.freecad` does not join. A `g` line that then ends in an
unpaired `\` is `GMDL_ERR_FORMAT`; `g a\\` and `o a\` are not.
`GMDL_Obj_Options.reject_vertex_continuation` fails the file on a `v` line
that ends in `\`, which is VTK 9.3, and does not join it.
`GMDL_Obj_Options.break_group_continuation` leaves that `\` in a `g` or `o`
name and keeps the next line, which is also VTK.

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
swap sides.

### 2.7 Directives

The first token on a line is the directive. Matching is exact and
case-sensitive, and the directive must be followed by whitespace or the end
of the line: `V` is not `v`, and `vertex` is not `v` followed by `ertex`.

A directive this library does not implement is ignored with its whole line.
**3.14 is the list**, and this section deliberately does not repeat it: the
set was written out in both places, the copy here fell behind, and for three
commits it described `s`, `l` and `p` as ignored while the parser was reading
them. One list, in the section whose subject it is - which as of 2026-09-24
is empty, so what this rule covers today is an exporter's private
extensions.

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

On Windows the pin is `_configthreadlocale` plus `setlocale`, which is
per-thread under the UCRT and MSVC. MinGW-w64 against the older `msvcrt.dll`
(MSYS2's MINGW64 environment) has no per-thread locale at all, so there the
Makefile defines `GMDL_ALLOW_PROCESS_WIDE_LOCALE`, described next.

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
| 4 | `x y z w`; `w` is the homogeneous weight |
| 5 | `x y z w`, and a fifth number that is neither a weight nor a colour |
| 6 | `x y z r g b` |
| 7 or more | `x y z r g b`, the seventh onwards ignored |

Every row was measured against Blender 4.3.2, which agrees on all of them -
including the last, where `x y z w r g b` would be the other reasonable
reading and neither importer takes it. The boundary between five and six is
the whole rule, so it is what the tests pin.

`w` is the homogeneous weight a rational curve multiplies its control
point by. `curv` and `surf` index `v`, and `cstype rat` says the element is
rational, so the weight belongs with the vertex and not with `vp` - `vp`'s
own third number is the weight of a parameter-space point (3.19). Four or
five numbers record it, in `GMDL_Obj_Weight`, parallel to the colours so a
file that never writes one pays nothing for it. An absent weight holds 1,
which leaves the point unweighted, and `present` says which: a file may
write `w` as 1. Six or more numbers are the colour extension instead, which
is what Blender does with those lines, so a vertex carries a weight or a
colour and not both. The dump writes four numbers only when `present` is
set.

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
`GMDL_Obj_Options.reject_extra_face_field` makes it `GMDL_ERR_FORMAT`.
`GMDL_Obj_Options.freecad` omits the face instead, and does the same for a
face that names a missing vertex. A quad is stored as two triangles,
corners (0, 1, 2) and (2, 3, 0). A face with any other count is omitted.
`GMDL_Obj_Options.omit_unresolved_faces` omits only the missing-vertex
face, and keeps every other corner count.

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

**Each word of a `g` line is a group.** The documentation permits `g a b c`
to put the following faces in three groups at once. Blender reads that line
as one group called `a b c`; this follows the documentation. The groups
share one face range, and `joined` on every name after the first says they
were one line, which is what lets the dump write `g a b c` back. Two `g`
lines would close the first group before the faces and leave it empty.

**An `o` line is one name**, spaces included, as for `mtllib` and `usemtl`
(3.7, 3.8). The specification gives an object a single name. Splitting
`o two words` would rename the object.

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
is two groups (3.6), and a material name may contain spaces.

Materials are not resolved by the OBJ parser. Each distinct name is assigned
an index in order of first use, recorded in `material_mappings`, and faces
carry that index. A repeated name reuses its index. The consumer resolves
names against an MTL library with `gmdl_mtl_find()`.

### 3.8 `mtllib path`

Names a material library. This library keeps the **whole of each `mtllib`
line** as one path, blanks at either end dropped, and keeps **every line**
the document carries, in order. A bare `mtllib` names no library: it
contributes no entry, so a dump does not write back a line the document
never had, and it clears nothing - measured 2026-09-23, Blender 4.3.2 reads
the line as an unrecognized element and still applies a material from a
library an earlier line named.

The documentation allows several paths on one line, and Blender does not
implement that: given `mtllib a.mtl b.mtl` it looks for a single file called
`a.mtl b.mtl`, finds nothing, and loads no materials at all. So the
whole-line reading is the reference's as well as this one's, and it is what
the reference *writes*: exporting a document saved as `my model.obj` produces
`mtllib my model.mtl`, with no unusual settings involved. Taking the first
token of that line named the file `my`.

Several `mtllib` **lines** are loaded by both references, and this library
now keeps all of them: `GMDL_Obj.mtllibs` is the list and
`GMDL_Obj.mtllib_count` its length. It kept only the last until 2026-09-23,
so a document naming two libraries lost one silently.

`GMDL_Obj.mtllib` remains, as the **first** path or `""` when there is none -
equivalent to `mtllibs[0].path`, and now derived from the list rather than
maintained beside it. It held the last path before the list existed, which
differs only for documents that were losing libraries anyway. Keeping it is
what made this a non-breaking change: the one consumer reads that field,
names one library, and did not have to move.

It was maintained beside the list for one commit, and the two disagreed
straight away: a bare `mtllib` after a real one cleared the field and left
the list alone, so a document with that shape held `mtllib_count == 1` and
`mtllib == ""`. The dump writes the list, so the reload came back with a
path the original had blanked - a round trip broken by the field that was
added to avoid breaking anything. **The fuzzer found it on the first run
after the list landed**, from a corpus document nobody wrote for the
purpose; the unit tests of the time asserted the clearing, because the
clearing was the behaviour being kept rather than the invariant being
checked. Deriving the field is what makes the two unable to differ,
which is why the fix is that rather than one more place to remember.

`GMDL_Obj_Options.max_mtllibs` caps the count, and like the other record caps it
defaults to 0, meaning the size of the input is the bound.

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

**Nothing, as of 2026-09-24.** Every directive this section once listed is
read; the section stays because what it got wrong is worth keeping.

It listed twenty-six directives in three groups and gave one reason for all
of them. Two of the three groups fell to writing the reason down. The nine
**render attributes** were three different things that this section's own
grouping had made look like one: four are per-element state (3.16), two are
file-level paths the blocker never applied to (3.17), and three belong to
the free-form sub-language (3.18). `maplib` and `usemap` were declined
because "almost nothing writes them", which is a claim about one exporter's
output, and because the work was "a second name-to-index mapping beside the
material one", which describes the work rather than arguing against it
(3.15).

The **free-form sub-language** was the one where the reason held: fifteen
directives describing NURBS curves and surfaces with trimming really is a
second data model rather than more fields on the polygon mesh. What followed
from that was *where to stop*, not whether to start. 3.19 records free-form
geometry and does not evaluate it: `vp`, the state and the elements landed on
2026-09-23, the body statements and `con` on 2026-09-24, and nothing
tessellates a surface or walks a trimming loop. A file carrying a `curv` used
to lose it in silence.

What remains not *done* is in 12, and it is a different kind of thing: places
where the specification and the two references disagree and a side has to be
picked. Those are decisions, not absences.

A line whose directive is none of 3.1-3.13 or 3.15-3.19 is skipped, which is
how a file carrying an exporter's private extension still loads.

### 3.15 `maplib path` and `usemap name`

The texture-map pair, standing to texture maps as `mtllib` and `usemtl` do
to materials. Both are read, and each mirrors its material counterpart
exactly.

`maplib` keeps the **whole of each line** as one path, blanks at either end
dropped, and keeps **every line** in order, in `GMDL_Obj.maplibs` with
`GMDL_Obj.maplib_count`. A bare `maplib` names no library and contributes no
entry, so the dump does not write back a line the document never had. There
is no compatibility scalar beside the list, because this list had no
predecessor to be compatible with.

`usemap` sets the texture map for the elements that follow - faces,
polylines and points alike, the same three `usemtl` reaches. The whole line
is the name (3.7), a bare `usemap` is `GMDL_ERR_FORMAT`, and an over-long one
is `GMDL_ERR_LIMIT` (3.9). Each distinct name gets an index in order of first
use, recorded in `GMDL_Obj.map_mappings`; a repeated name reuses its index;
elements carry the index in `map_index` and the consumer resolves the name.
This parser opens nothing.

**`usemap off` is the one thing `usemtl` has no equivalent for.** It returns
the state to "no map", which is `map_index == -1` - the same value an element
declared before any `usemap` carries, because the format cannot tell those
two apart either. `off` is reserved, so a texture map genuinely called `off`
cannot be named; that is the trade `s off` already makes for smoothing
groups (3.11), and it is the format's own. It also makes the dump simpler
than the material one: section 9 writes polylines and points in two passes
because OBJ cannot turn a *material* off, and maps need none of that, since
every state a parse can produce has a spelling.

**No reference reads either directive.** Measured 2026-09-23: Blender 4.3.2
given a file carrying `maplib a.map b.map`, `usemap chrome` and `usemap off`
prints `OBJ element not recognized` for all three and loads the geometry
without them. So the documentation's several-paths-on-one-`maplib`-line
reading is settled here by this library's own rule rather than by a
reference, which is the difference from 3.8 - there, Blender's handling of
`mtllib a.mtl b.mtl` was the evidence.

**The reason for leaving them out did not hold.** 3.14 gave two: "almost
nothing writes them", and that reading them "means a second name-to-index
mapping beside the material one". The first is a claim about the corpus, and
the corpus is one exporter's output; the second is a description of the work,
and the work is one array and one `int32_t` per element, reusing the shape
`usemtl` already has. Neither is a reason a file that carries them should
lose them.

`GMDL_Obj_Options.max_maplibs` and `GMDL_Obj_Options.max_maps` cap the two counts, and
like every other record cap they default to 0, meaning the size of the input
is the bound.

### 3.16 `bevel`, `c_interp`, `d_interp` and `lod`

Four render attributes. Each is state, like `usemtl`: it applies to every
element after it - faces, polylines and points alike - until the next one
changes it. None of them changes any geometry; they describe how a renderer
should draw it.

| directive | argument | default |
| --- | --- | --- |
| `bevel` | `on` or `off` | `off` |
| `c_interp` | `on` or `off` | `off` |
| `d_interp` | `on` or `off` | `off` |
| `lod` | an integer, documented as 0 to 100 | `0` |

A switch whose argument is neither word - including a bare one - is
`GMDL_ERR_FORMAT`: the format defines two spellings and there is no third
reading to guess at. Trailing text is ignored, so `bevel on please` is `on`,
which is the reading `s` and every number in this parser already use. A
`lod` with no number is `GMDL_ERR_FORMAT` and one too wide for an `int32_t`
is `GMDL_ERR_LIMIT`, as it is for `s` (5).

**`lod` is kept as written, in the documented range or outside it.** A file
saying `lod 200` holds 200. This is the call `GMDL_Obj_Color` makes for a
colour outside `[0, 1]` and the one section 1 argues for generally: what an
out-of-range value means is a consumer's decision, and a parser that
corrected it would leave the file unrecoverable.

**The four live in a record elements point at, not in four fields on every
element.** `GMDL_Obj.render_states` holds one `GMDL_Obj_Render_State` per
distinct combination the document put in force, in order of first use - the
way `usemtl` names are assigned - and each element carries a
`render_index` into it. The all-defaults state takes no record and is named
by `-1`, so a document mentioning none of the four holds no records and
every element says `-1`.

That is the answer to 3.14's objection, which was that recording these would
put a field on every face "describing something no consumer of this library
asks about". It would have: four fields, on all three element kinds.
Measured on x86-64 with one index instead: a face stays 80 bytes and a point
stays 16, because the index lands in padding they already carried, and a
polyline grows from 24 to 32. There is one polyline per `l` statement
against one face per `f`. A fifth attribute of this kind widens the record
and no element.

Every attribute has a spelling for its own default, so the dump can always
write a return to them - which is not true of `usemtl` (9), and is why these
need none of the two-pass handling polylines and points need for materials.
The dump writes **only the attributes that change**, so a document that sets
`bevel on` once carries one `bevel` line however many elements follow.

**No reference reads any of them.** Measured 2026-09-23: Blender 4.3.2 given
a file carrying all nine of 3.14's render attributes prints `OBJ element not
recognized` for every one and loads the geometry without them.

`GMDL_Obj_Options.max_render_states` caps the number of distinct combinations.

### 3.17 `shadow_obj path` and `trace_obj path`

Two paths to other OBJ documents: the object that casts shadows for this one,
and the object used for reflections when ray tracing. Both are **recorded and
never opened**, the way `call` is (3.13) - and the same warning applies more
sharply, because these name a document that would then be parsed. A consumer
that resolves either must treat the path the way section 8 says to treat a
texture map path.

Both read the **whole line** as one path, blanks at either end dropped, and a
bare directive names nothing and contributes no entry - the reading 3.8 makes
for `mtllib`, for the reason it gives. A path too long for the field is
`GMDL_ERR_LIMIT` (3.9).

**Both are lists, though the specification says one per file.** `You can use
only one shadow object per file` is a statement about conforming documents,
not about what arrives. A scalar would make a document carrying two lose one
without saying so, which is precisely the defect `GMDL_Obj.mtllib` had (3.8)
and precisely the reason that field is now derived rather than maintained. A
conforming document gives `GMDL_Obj.shadow_objs` one entry.

Their position relative to the geometry is not recorded, for the reason 3.13
gives for `call` and `csh`: nothing else in this model is ordered against the
geometry, and a directive the specification calls one-per-file has no
position to preserve anyway. The dump writes them with the other paths, before
the vertices.

`GMDL_Obj_Render_Object` is one type for both, where `GMDL_Obj_Mtllib` and
`GMDL_Obj_Maplib` are two: those name libraries of definitions in two
different formats, and these both name an OBJ file. The type says what the
path points at.

`GMDL_Obj_Options.max_shadow_objs` and `GMDL_Obj_Options.max_trace_objs` cap the two
counts.

### 3.18 `ctech`, `stech` and `mg`

The last three of what 3.14 used to call the render attributes, and the only
ones that are not about the polygon mesh at all. `ctech` and `stech` set how
a curve or a surface is approximated; `mg` sets the merging group and
resolution for the free-form surfaces that follow, with `mg off` turning
adjacency detection off.

All three are **state for the free-form sub-language**, the way `cstype` and
`deg` are (3.19), so each applies to every element after it until the next
one changes it, and each is recorded on
`GMDL_Obj_Freeform.state` - alongside the basis, the degree and the step. An
element declared before any of the three carries none of them; one declared
after carries what was in force. A list in file order can answer neither
question, which is why these are not one.

They were **text** until 2026-09-24, in a `GMDL_Obj.freeform_attrs` array
this version removes. That was a deliberate placeholder: parsing them into
typed records before the model they describe existed would have meant
choosing a representation and attaching it to nothing. 3.19 landed the model
on 2026-09-24 and this landed the same day, which is the breaking change the
previous version of this section documented as coming.

#### The techniques

`ctech` and `stech` name a technique and then the numbers it calls for:

| line | meaning |
| --- | --- |
| `ctech cparm res` | constant parametric subdivision |
| `ctech cspace maxlength` | constant spatial subdivision |
| `ctech curv maxdist maxangle` | curvature-dependent subdivision |
| `stech cparma ures vres` | constant parametric, u and v separately |
| `stech cparmb uvres` | constant parametric, one resolution |
| `stech cspace maxlength` | constant spatial subdivision |
| `stech curv maxdist maxangle` | curvature-dependent subdivision |

The seven rows are the reference's, argument names and all, and the arity
column of the table the parser and the dumper share is read from them.

`GMDL_Obj_Ctech` and `GMDL_Obj_Stech` are **two enumerations, not one**.
`cparm` is a curve technique and `cparma` a surface one, and the words are
close enough that a single lookup would accept each in the other's line
without anything noticing - a file would come back saying a curve is drawn by
a rule the format gives only to surfaces. Here `ctech cparma 4 8` is
`GMDL_ERR_FORMAT`, and so is an unrecognised technique, for the reason 3.19
gives for an unrecognised `cstype`: the format defines the spellings and
there is no other reading to guess at.

**A technique carries exactly the numbers its row names.** `ctech curv 0.5`
is `GMDL_ERR_FORMAT`, not a `maxangle` of zero - the technique fixes the
arity, so a short line has lost a number rather than chosen a shorter form,
and recording a 0 would put a value into the model the document never wrote.
This is where these two part company with `deg` and `step`, which the format
itself gives both a one-number and a two-number form (3.19); there a count
records which was written, and here there is nothing to count.

Each number must be a **whole token**: `ctech curv 1.5.2` is
`GMDL_ERR_FORMAT` rather than `1.5` followed by `0.2`, the same rule and the
same reason as the body statements in 3.19.

#### `mg`

`mg off`, or `mg group [res]`. `GMDL_Obj_Freeform_State.merge` is
`GMDL_OBJ_MERGE_NONE`, `_OFF` or `_ON`; three states rather than a `bool`,
because a file that said `mg off` and a file that said nothing are different
documents and a dump that wrote `mg off` for the second would put a directive
into a file that had none.

**The resolution is optional, and the reference says so.** Bourke's page
writes the syntax as `mg group_number res`, then says of `res` that it "is a
required argument only when using merging groups" and of `group_number` that
turning adjacency detection off takes "a value of 0 or off". So a line that
disables merging carries no distance, and refusing `mg 0` would reject a
document saying exactly what the format tells it to say. `merge_count`
records whether a `res` was there, for the reason `degree_count` exists:
`mg 1` and `mg 1 0` are different lines and no float value can stand for
"absent".

A group of 0 is **not** folded into `_OFF`. Which spelling the file used is
kept, the call `g` and `o` get in 3.6: a consumer acting on the state reads
the two alike, and one writing the file back writes what it was given.

A second token that is not a number - `mg 1 half` - is `GMDL_ERR_FORMAT`.
That is stricter than `v`, where everything after the numbers is a field the
format does not define (3.1); here the second token *is* a field the format
defines, so a line that says a resolution and does not give one is malformed
rather than decorated. A group outside `int32_t` is `GMDL_ERR_LIMIT`, the
answer `s`, `lod` and `illum` already give.

#### Caps

None. These are fixed-size fields on a record 3.19 already caps with
`GMDL_Obj_Options.max_freeforms`, so there is nothing here a document can make
arbitrarily large. `max_freeform_attrs`, which capped the text array, is
removed with it.

### 3.19 The free-form sub-language

Fifteen directives describing NURBS curves and surfaces: `vp`, `cstype`,
`deg`, `bmat`, `step`, `curv`, `curv2`, `surf`, `parm`, `trim`, `hole`,
`scrv`, `sp`, `end` and `con`. All fifteen are read as of 2026-09-24. 3.14
declined all of them together, and the reason it gave was right: this is a
second data model, not another field on the polygon mesh.

**What follows from that is where to stop, not whether to start.** This
library *records* free-form geometry and does not *evaluate* it. Nothing
here tessellates a surface, walks a trimming loop or resolves a basis
matrix; a consumer that wants a mesh out of a NURBS patch has to do that
itself, with what this parser hands it. The split is deliberate, and it is
the whole of why the work is tractable: evaluating a surface is a different
project from not discarding one. A file that carries a `curv` keeps it.

#### `vp u [v] [w]` - parameter-space control points

The control points the free-form elements are built from. These are **not**
`v` records and are **not** numbered with them: `vp` has its own index
space, which `curv2` and `sp` count into while `curv` and `surf` count into
`v`. A reader that merged the two arrays would resolve every free-form
reference to the wrong point, and would do it silently, because both
numberings start at 1.

Only `u` is required. A `curv2` control point is one-dimensional, a
surface's is two, and a rational one carries a weight as the third - so the
line's *arity* is part of what it says, and this library records it in
`GMDL_Obj_Param_Vertex.count` rather than normalising every point to three
numbers. `vp 0.5` and `vp 0.5 0` are different statements; without the
count they become the same record and the dump has to guess which to write.

The coordinates a line did not carry hold the format's own defaults, 0 for
`v` and 1 for `w`, so a consumer that ignores the count still reads
something rather than whatever the allocation held. A line with no numbers
at all is `GMDL_ERR_FORMAT`, as `vt` with none is.

`GMDL_Obj_Options.max_param_vertices` caps the count.

#### `cstype`, `deg`, `bmat` and `step` - the free-form state

Four directives saying how the control points of the elements that follow
are to be read. Each is state in the file exactly as `usemtl` is, and
together they are what makes a `curv` more than a list of indices. `ctech`,
`stech` and `mg` join them on the same record (3.18); they are documented
apart because they say how finely the result is drawn rather than what it
is, and an element naming none of them is still complete.

- **`cstype [rat] type`** names one of five bases: `bmatrix`, `bezier`,
  `bspline`, `cardinal` or `taylor`. `rat` is a *prefix* making the curve or
  surface rational, not a sixth basis, and it is kept as its own field for
  that reason - folding the two into one enumeration would leave "rational"
  unspellable for a basis nobody has written a rational example of. A word
  that is none of the five is `GMDL_ERR_FORMAT`, for the reason 3.16 gives
  for `bevel junk`.
- **`deg degu [degv]`** and **`step stepu [stepv]`** each carry one number
  for a curve and two for a surface. **How many the line carried is
  recorded**, in `degree_count` and `step_count`, rather than marking an
  absent number with a sentinel value - the shape `vp` uses above, and for a
  sharper version of the same reason. -1 was the sentinel at first, and
  `step -1 1` is a line a file may write: the dump read that real state as
  "none in force", wrote nothing, and the reload lost the second number. The
  fuzzer found it in ten minutes. Every `int32_t` is a value some file can
  write, so no value can stand for absence; a count can. A number outside
  `int32_t` is `GMDL_ERR_LIMIT`, the answer `s` and `lod` already give.
- **`bmat u|v values...`** supplies the basis matrix `cstype bmatrix` needs.
  Its length is `(deg + 1)` squared and **this is not checked**: the degree
  comes from a separate directive that a file may state afterwards, so
  refusing the line here would reject a document whose directives are merely
  in an order this parser did not expect. The values live in
  `GMDL_Obj.basis_values`, a flat array each state names a span of, because
  the length is not knowable from the directive alone.

**The state is held on each element, not indexed.** This is the opposite of
what 3.16 does with the render attributes, and the reason is counts rather
than taste: there is one face per `f` and a document has millions, so a
field on a face is a field a document pays for everywhere; there is one
`surf` per patch and a document has dozens. `GMDL_Obj_Freeform.state` is a
copy of what was in force where the element began.

#### `curv`, `curv2`, `surf` and `end` - the elements

- **`curv u0 u1 v1 v2 ...`** is a curve in model space: two numbers of
  parameter range, then control points indexing `GMDL_Obj.vertices`.
- **`curv2 vp1 vp2 ...`** is a curve in parameter space: no range, and
  control points indexing `GMDL_Obj.param_vertices` instead. **The two index
  different arrays**, both numbered from 1 in the file, so a reader that got
  this wrong would resolve every reference to a real point of the wrong kind
  and never be told.
- **`surf s0 s1 t0 t1 v1/vt1/vn1 ...`** is a surface: four numbers of range,
  then references in the `v/vt/vn` form `f` uses (3.5), resolved the same
  way. Only `surf` has that form; a `v/vt` token on a `curv` is
  `GMDL_ERR_FORMAT` rather than a reference with the extra fields dropped,
  because dropping them would lose what the file said while reporting
  success.

All three take relative (negative) indices, measured against the counts at
that point in the file, exactly as `f` does - and `curv2` measures its
against the `vp` count, which is the whole reason `vp` has an index space of
its own.

An element naming **no** control points is `GMDL_ERR_FORMAT`, and so is one
whose parameter range is short: unlike `vt`'s optional second number, the
specification gives `curv` and `surf` no shorter form and a missing `u1`
would have to be invented.

**`end` is read and not recorded.** It closes an element, and a closed
element and an unclosed one hold the same data - the only thing `end`
decides is which element a body statement belongs to, and that is answered
by the time parsing finishes. A file that omits it is not refused. The dump
writes one after every element, because the specification asks for it and a
reader that needs it to know where an element stops would otherwise read the
next directive as part of this one.

`GMDL_Obj_Options.max_freeforms` caps the elements, `max_basis_values` the `bmat`
values across every line, and `max_face_indices` the references in one
element - the same budget it already applies to one `f` and one `l`, because
it is the same quantity.

#### `parm`, `trim`, `hole`, `scrv` and `sp` - the body statements

Five directives that stand between an element and its `end` and describe the
element they stand in. `parm` gives the knot vector, `trim` and `hole` the
outer and inner trimming loops, `scrv` a special curve and `sp` the special
points.

**Each line is its own record**, in `GMDL_Obj.freeform_bodies`, and each
element names the span of them that belongs to it. That is not the shape
`bmat` uses, and the difference matters for three of the five: **each `trim`
builds a separate loop**, and so does each `hole` and each `scrv`. Merging
two `trim` lines into one list of curve references would join two loops into
one and change the shape the file describes - while still round-tripping
through this library's own dump, because the merged model writes one `trim`
and reads one back. A loss that agrees with itself is the kind this library
is built against.

The span is kept across all five kinds in file order rather than grouped by
directive, because `trim` and `hole` interleave to describe a surface with
holes in it and the order they were written in is the order they have to be
written back in.

`GMDL_Obj_Freeform_Body.kind` says which array its span indexes, because the
five carry three different payloads: `parm` floats in `GMDL_Obj.parm_values`,
the three loop directives curve references in `GMDL_Obj.curve_refs`, and `sp`
indices in `GMDL_Obj.special_points`.

- **`parm u|v p1 p2 ...`** is the one that carries a direction, and unlike
  `bmat`'s the direction picks which *record* the line becomes rather than
  which of two spans it fills. A direction that is neither `u` nor `v`, and a
  bare `parm`, are `GMDL_ERR_FORMAT`.
- **`trim`, `hole` and `scrv`** each take curve references in threes: `u0`,
  `u1`, and the index of a `curv2`. A triple that stops short is
  `GMDL_ERR_FORMAT` rather than kept as far as it got - two of the three name
  a range with no curve in it, and the format gives them no shorter form.
- **`sp vp1 vp2 ...`** names points in parameter space, so it counts into
  `vp` **whatever kind of element it belongs to** - including a `curv`, whose
  own control points count into `v`. It is the one free-form reference that
  does not change array with the element's kind.

**A body statement outside an element is `GMDL_ERR_FORMAT`.** It describes
the element it stands in, so there is nothing to attach it to: recording it
against the previous element would change which patch is trimmed, and
dropping it would lose what the file said while reporting success. `end`
closes an element, and so does declaring the next one - which is what lets a
file that omits `end` still attach each statement to the element it was
written under.

**The whole token has to be a number.** `strtof()` stops at the first
character it cannot use and reports success for what it read, which is how
the `bmat` and parameter-range loops find the end of a list. A body statement
cannot afford that: `trim 0 1 2.5` would read the index as `2`, leave `.5`,
and take that as the next triple's `u0` - accepting a line as a different
line, silently. So a token that begins with a number and continues into
something else is refused.

`GMDL_Obj_Options.max_freeform_bodies` caps the statements across every element,
`max_parm_values` the `parm` values, `max_curve_refs` the references `trim`,
`hole` and `scrv` name, and `max_special_points` the `sp` indices.

#### `con` - joining two surfaces

`con surf_1 q0_1 q1_1 curv2d_1 surf_2 q0_2 q1_2 curv2d_2` says that two
surfaces meet, and which curve in each one's parameter space they meet along.
It is the one free-form directive that is neither state nor a body statement:
it stands at file level, names its surfaces, and neither needs an open
element nor closes one. All eight numbers or none - a `con` naming one
surface and half of the other describes no join.

Recorded and not acted on. Nothing here checks that the surfaces exist, that
the curves lie in their parameter spaces, or that the join is geometrically
possible. The dump writes connections after every element, because each names
its surfaces by ordinal and a `con` written first would name patches a reader
has not seen.

#### How `trim`, `hole`, `scrv` and `con` number what they name

These four reference a `curv2` or a `surf` **by its ordinal within its own
kind**, which is how the format numbers everything else: `vt 2` is the second
`vt`, not the second line of the file. `GMDL_Obj.freeforms` holds all three
kinds together in file order, so the two numbers part company as soon as a
document mixes them - `trim 0 1 2` names the file's second `curv2`, which may
be `freeforms[3]`.

`gmdl_obj_freeform_of_kind()` is the way across, and it is a search rather
than a second index array because the population is dozens of patches: a
parallel array would cost every document memory so that trimmed surfaces,
which few documents have at all, could skip a walk.

**The ordinal is held rather than resolved**, for the same reason
`GMDL_Obj_Face` holds an out-of-range vertex index. A positive index counts
from the start of the file and nothing in the format says the curve it names
has been read yet, so resolving here would mean refusing a forward reference
the format allows. Range checking is the consumer's (section 1), and an index
that cannot be resolved *yet* is not an index that is wrong.

**This numbering is a reading, not a quotation.** The specification names the
argument `curv2d` in one place and `surf` in another, which is what makes
per-kind numbering the consistent reading; it does not say so in as many
words, and neither reference reads any of these directives, so nothing
external settles it. Recorded here so that a consumer disagreeing knows which
call to undo.

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

The colour properties have three documented forms, and all three are
recorded. `K? r g b` is the ordinary one. `K? r` - one value, meaning
`r r r` - is accepted and expanded. `K? xyz x y z` is CIE XYZ: the three
numbers are stored as stated and not converted to RGB, because that
conversion is a colour-space decision and the dump would then be unable to
write `xyz` back. `GMDL_Mtl_Color.form` says which. `K? spectral file
[factor]` records the path and does not open it, the way an OBJ `mtllib`
path is not opened; the three numbers are left at zero, because the file
stated no colour. The factor defaults to 1 when the line omits it, and
`factor_stated` says which, because a written `1` and an omitted factor are
different lines. A `xyz` with fewer than three numbers, a `spectral` with
no file, and a token after the file that is not a factor are
`GMDL_ERR_FORMAT`.

`d -halo n` is the same dissolve as `d n`, with `d_halo` set. The keyword
says the dissolve depends on the surface orientation. A later plain `d`
clears it. `d -halo` with no number is `GMDL_ERR_FORMAT`.

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

## 5. Options

Each format has its own options struct. A later format does not inherit
either of these: a cap on `curv` means nothing to a material file, and a
binary format has no line length. `NULL` passed to a load is that format's
defaults. The result codes, the allocator and the stream are what the
formats share.

### 5.1 OBJ

`GMDL_Obj_Options`, with `gmdl_obj_options_default()`:

| Field | Default | Counts |
| --- | --- | --- |
| `max_line_length` | `GMDL_DEFAULT_MAX_LINE_LENGTH` (65536) | bytes in one line, excluding its ending |
| `max_vertices` | 0 (unlimited) | `v` records |
| `max_texcoords` | 0 | `vt` |
| `max_normals` | 0 | `vn` |
| `max_param_vertices` | 0 | `vp` |
| `max_faces` | 0 | `f`, `l` and `p` elements **together**, one budget |
| `max_face_indices` | 0 | references in one `f`, `l`, `curv`, `curv2` or `surf` |
| `max_groups` | 0 | `g` and `o` together |
| `max_materials` | 0 | distinct `usemtl` names |
| `max_statements` | 0 | `call` and `csh` records |
| `max_mtllibs` | 0 | `mtllib` records |
| `max_maplibs` | 0 | `maplib` records |
| `max_maps` | 0 | distinct `usemap` names |
| `max_render_states` | 0 | distinct render-attribute combinations |
| `max_shadow_objs` | 0 | `shadow_obj` records |
| `max_trace_objs` | 0 | `trace_obj` records |
| `max_freeforms` | 0 | `curv`, `curv2` and `surf` elements together |
| `max_basis_values` | 0 | `bmat` values, across every line |
| `max_freeform_bodies` | 0 | `parm`, `trim`, `hole`, `scrv` and `sp` lines together |
| `max_parm_values` | 0 | `parm` values, across every line |
| `max_curve_refs` | 0 | curve references in `trim`, `hole` and `scrv` |
| `max_special_points` | 0 | `sp` indices, across every line |
| `max_connections` | 0 | `con` records |
| `accept_short_vertex` | false | `v` with one or two numbers is padded with zero |
| `reject_extra_face_field` | false | a fourth `/` field is `GMDL_ERR_FORMAT` |
| `reject_non_finite` | false | `nan` and `inf` are `GMDL_ERR_FORMAT` |
| `freecad` | false | FreeCAD 1.0 ReaderOBJ, below |
| `non_finite_becomes_zero` | false | `nan` and `inf` on `v`, `vt`, `vn`, `vp` become 0 |
| `keep_byte_order_mark` | false | a leading UTF-8 BOM stays on the first line |
| `omit_unresolved_faces` | false | a face naming a missing vertex is omitted |
| `join_before_comment` | false | `\` is seen before a comment is cut |
| `reject_vertex_continuation` | false | a continued `v` is `GMDL_ERR_FORMAT` |
| `reject_face_comment` | false | `#` on an `f` line is `GMDL_ERR_FORMAT` |
| `break_group_continuation` | false | `\` on `g` or `o` stays in the name |

`freecad` is the reading measured against FreeCAD 1.0.0's `ReaderOBJ` on
2026-09-28. It turns off the line rewriting in 2.1 and 2.4 through 2.6, omits
a `v` with fewer than three numbers or a non-finite one without giving it an
index, omits a face it cannot use, and splits a quad as 3.5 describes. A `g`
line ending in an unpaired `\` is `GMDL_ERR_FORMAT`. Vertices no surviving
face uses are still recorded; FreeCAD's mesh count is only the points a
facet uses. Set, it wins over `accept_short_vertex`, `reject_extra_face_field`
and `reject_non_finite` where they describe the same element: the element is
omitted rather than padded or rejected.

### 5.2 MTL

`GMDL_Mtl_Options`, with `gmdl_mtl_options_default()`:

| Field | Default | Counts |
| --- | --- | --- |
| `max_line_length` | `GMDL_DEFAULT_MAX_LINE_LENGTH` (65536) | bytes in one line, excluding its ending |
| `max_materials` | 0 | `newmtl` records |
| `accept_map_without_path` | false | a map directive with no path is skipped |
| `reject_non_finite` | false | `nan` and `inf` are `GMDL_ERR_FORMAT` |

`0` means no limit - **except for `max_line_length`**, where it means
`GMDL_DEFAULT_MAX_LINE_LENGTH`. That one field is the odd one out on purpose:
its buffer is allocated once, before the first line is read, so "no limit"
would be an unbounded allocation a caller could ask for by leaving a field
out. A caller who zeroes this struct and fills in only the fields it cares
about gets no cap on anything it did not mention and a bounded line either
way, which is the safe reading of an omission. When a record would take a
count from `limit` to `limit + 1`, the result is `GMDL_ERR_LIMIT` and parsing
stops.

`max_faces` is one budget across three arrays, not one each. It was three
separate checks against the same field until it was measured, so a file with
one `f`, one `l` and one `p` loaded under a cap of two - a caller bounding
memory from untrusted input was getting three times the bound they set, on
the one axis the field exists for. The whole suite passed either way, because
every case exercised one element kind at a time: a limit spanning several
arrays needs a case that spans them. A `p` statement costs one per index it
names rather than one per line, because that is what the model stores
(3.10). `NULL` options
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
allocator, and both are written as invariances rather than as thresholds: the
high-water mark does not move when the input quadruples under unchanged caps,
and it does not move when one element's index count grows tenfold. Neither
has a number in it that somebody chose.

That is not only about a threshold rotting. A bound assertion fails with one
number against a limit, which says the peak is too big; an invariance fails
with two peaks against each other, which says the peak is *tracking the
input* - and that is the defect rather than a symptom of it. Measured
against the deferred-refusal version below, the failure reads "a 10,000-index
face peaked at 1207205 bytes and a 100,000-index one at 2656373", the
difference being ten times the shared line buffer. The second was seen to fail against a version that answered
`GMDL_ERR_LIMIT` for exactly the same files and allocated the whole face
first; every status-keyed test passed against it.

Section 1 promises a cap on every unbounded quantity, and `max_statements`
was missing from this table until it was measured for: with every other field
set, a file of nothing but `call` lines was still accepted without bound. The
fuzzers cannot find that class of gap, because what they drive is the set of
caps that *exist* - a quantity with no field is invisible to them. What
catches the next one is a test that walks `GMDL_Obj_Options` cap by cap and
requires each to refuse something, which fails if a field is added without
enforcement.

**A field is not one gate, and that table only covered fields.** A limit is
read wherever the parser counts the thing it caps, and each of those is a
separate branch that can be wrong on its own: `max_face_indices` guards a
face and a polyline, `max_statements` guards `call` and `csh`, `max_faces`
guards three element kinds. One document per field proved each *field*
refused something and said nothing about the other sites - measured by
deleting the polyline and `csh` checks outright, which the suite did not
notice. The table lists one document per site now.

---

## 6. Results

| Result | When |
| --- | --- |
| `GMDL_OK` | parsed; `*out` is set |
| `GMDL_ERR_INVALID` | `out` or `stream` is `NULL` (`path` for `_file`) |
| `GMDL_ERR_IO` | `_file` could not open or read the path, for any reason including a path too long for the filesystem |
| `GMDL_ERR_FORMAT` | a line was malformed (sections 3 and 4) |
| `GMDL_ERR_UNSUPPORTED` | a map option this library does not implement (4.5) |
| `GMDL_ERR_LIMIT` | an options cap was exceeded, or a name or path was too long |
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
intention.

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

`vp` is written with exactly as many numbers as the line that made it
carried, because the count is what separates a curve's control point from a
surface's (3.19). Writing all three back would change what the statement
says, and this library's own reload would then come back with a different
model.

Free-form elements are written with the polylines and the points, in the
same two passes and for the same reason - they take a material too. Their
state directives are written the way the render attributes are, only where
one of the seven differs, and the basis spans are compared rather than the
values: within one parse two elements share a span exactly when they share a
matrix, and for a hand-built model the comparison errs towards writing a
`bmat` twice, which reloads the same.

The technique and merging directives are written after the rest of the
state, so that a run of elements differing only in how finely they are drawn
writes its geometry state once. A technique is written with exactly as many
numbers as its row in 3.18 gives it, from the same table the parser reads, so
a technique cannot be written with more numbers than it is read back with. An
`mg` whose `merge_count` is 1 writes the group alone - writing `mg 2 0`
instead would put a distance into the file the document never gave.

Each element's body statements are written between it and its `end`, one
line per record and in the order the file wrote them (3.19). A body span or
an entry span that leaves its array writes **nothing at all** rather than a
directive with no entries after it - a bare `trim` is a line this parser
refuses, so the alternative would be a dump of a hand-built model that fails
to reload. So is a `kind` outside the enumeration, which has no directive to
spell it with. A parse can produce neither; a caller assembling a model by
hand can produce both.

Connections are written last, after every element, because each names its
surfaces by ordinal (3.19) and a `con` written first would name patches a
reader has not seen.

The dump writes `usemtl` when the material changes, `usemap` when the
texture map does, `s` when the smoothing group does, a render attribute when
that one attribute changes, `g` for each group before its faces, and
relative indices as absolute ones.

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
in force. The free-form state adds six more of that last shape - a
`cstype`, `deg`, `step`, `bmat`, `ctech` or `stech` returning to "none in
force" - and none of them is reachable from a parse either, for the same
reason: each directive only ever sets a value and the format gives none of
them an "off". `mg` is the exception among the seven and is worth the
sentence: `mg off` is a spelling the format does have, so `_OFF` writes a
line and only `_NONE` writes nothing. Section 10 says what each one is and
how the fuzzers account for it.

That last one is not a state a parse can reach: `material_index` moves from
-1 to a mapping and never back, because OBJ can change the material in force
but cannot turn it off. A model built by hand can hold it, and the dump then
writes nothing, so the reload gives the element the previous material.

**None of that applies to texture maps**, which is worth saying because the
two look alike everywhere else. `usemap off` is a spelling for "no map", so
an element naming none while one is in force writes correctly, the two-pass
split buys maps nothing, and `map_index` moving back to -1 is an ordinary
state rather than an unwritable one. The one arm with no parse that reaches
it is a `map_index` naming no mapping, which a hand-built model can hold and
the dump writes as `usemap off` - the honest answer, where the material
fallback can write `usemtl white` only because the format says what an
unnamed material looks like.

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

**Allocation failure.** Every allocation either loader makes is refused in
turn, by `tests/unit/test_allocator.cpp`, the way `FailingSink` refuses the
dumper's writes. The documents are sized so that every array the builders hold
reallocates *during* the parse rather than only at setup, and
`tools/check-lists.py` holds them to that: raising an initial capacity past
the sweep's record count would silently return it to measuring the setup, and
nothing else would notice.

For each refused allocation the sweep asserts the result is `GMDL_OK` or
`GMDL_ERR_OOM` and nothing else, that no block is left outstanding, that
`GMDL_ERR_OOM` comes with no model at all, and - when the refusal is survived
- that the model produced is byte-for-byte the one an unrefused parse
produces. That last check is the load-bearing one. Not every allocation is
load-bearing: `gcu_array_shrink_to_fit()` is called for its effect and its
answer is discarded on purpose, so refusing it is correctly not an error. The
question is therefore not whether a refusal was survived but whether surviving
it changed the answer, and the dump is what a consumer would see.

**Three ways of failing, because no one of them reaches all of it.** Refusing
the *n*th request alone, refusing it and the one after, and refusing
everything from it onwards are each the only setting that catches some defect:

| Refuse | What only it reaches |
| --- | --- |
| request *n* | a single non-retried allocation whose arm loses data quietly, where refusing its successor too would mask it behind a correct failure |
| requests *n*, *n*+1 | a failed *array append*. cutil's `reserve_n()` answers a refused 1.5x growth by retrying at the exact size needed, so one refused request never fails an append at all |
| *n* onwards | an append's failure arm under exhaustion - but never a survived one, so a quiet give-up there reads as a correct failure |

Each row was added because a defect planted in the loader was invisible to the
rows above it, and all three were seen to catch one: a face overflow freed on
no error path, a colour-padding loop changed to give up quietly, an arm
returning `GMDL_ERR_FORMAT` for an allocation failure, and a recorded
statement dropped rather than reported.

Coverage says the arm ran; it does not say anything would notice the arm
misbehaving. The stronger measurement is mutation, and it was made: every
`GMDL_ERR_OOM` in both loaders - twenty-two of them - was changed in turn to
`GMDL_ERR_LIMIT`, with a clean control run between each. **Twenty-one were
killed, every one of them by the sweep and by nothing else.** The
twenty-second is the zero-capacity call in `obj_load.c` whose arm cannot run
at all, which is the answer a second instrument should give about a line the
first one reported uncovered: unreachable code is exactly the code mutation
cannot kill, and the two agreeing is what turns "explained" into "checked".

**The stream allocates too, and nothing drove its failure.** Every sweep
above hands the loaders a stream built with the default allocator, so a
refusal never reached the one `calloc` in
`gmdl_stream_create_memory_with_allocator()`. The file entry points are swept
separately for that reason - against a small document, since the loaders' own
arms are covered above and re-sweeping them through a file open buys nothing.
The arm worth having is in `gmdl_stream_create_file()`: it frees the file's
contents when the stream cannot be wrapped around them, which needs the read
to succeed and the very next allocation to fail, and where a missing free
would leak the whole file rather than report wrongly. Seen to fail by removing
that free.

**The corpus drives it too.** Both fuzz harnesses now take the top two bits
of their options byte as a refusal width - none, one request, one append, all
of them - and two further bytes as which allocation to refuse. Each input is
parsed twice: once with nothing refused, to establish what the document costs
and what it produces, and once with the refusal, held against that. Refusing
an allocation may only turn a result into `GMDL_ERR_OOM`; it may never change
the diagnosis, never rescue a document the reference refused, and never - when
survived - produce a different model.

**The choice has to be bounded by the measured cost, and getting that wrong
is silent.** The first version took a 16-bit number straight from the input.
Against documents that allocate a dozen times, essentially every choice landed
past the end, so the refusal never fired and the harness reported nothing
while looking entirely healthy. Taking it modulo the reference parse's request
count fixed it. The minimal pair, one plant and one corpus replay, changing
only the bounding:

| harness | planted statement-drop defect |
| --- | --- |
| 16-bit choice, unbounded | 11,522 files, nothing reported |
| same choice modulo the request count | caught in four seconds |

Both harnesses have since been seen to fail that way, each against a loader
arm changed to drop its allocation error quietly.

**The two reach different things, and neither is the other's substitute.**
The unit sweep walks *every* allocation of a document built to make every
array grow; the fuzzers walk *one* allocation each of tens of thousands of
documents nobody designed. Measured, against the working harness rather
than the broken one: a defect that only a survived refusal can reveal - the
colour-padding loop changed to give up quietly - is caught by the unit sweep
immediately and was **not** caught by the fuzzer in a full corpus replay
followed by 1,982,029 executions. Reaching it needs a document with more than
a hundred vertices carrying a late colour, which is far past the sizes
libFuzzer generates from this corpus. The reverse is the point of adding the
fuzzers: they vary document shape, which the unit sweep does not vary at all.

So the honest joint claim is "every arm runs, survived refusals are lossless
on one designed document and on every corpus document, and neither instrument
sees the large-document shapes the other would need". Section 12 keeps what
that still leaves open.

**Strict aliasing, which no runtime gate can see.** ASan, UBSan and valgrind
do not report a strict-aliasing violation, and this is not a matter of the
violation being theoretical. On a minimal pair gcc genuinely miscompiles -
store through `int *`, store through `float *`, reload the `int *` - a binary
built with *this library's own* sanitizer flags printed the miscompiled
answer at -O2 and -O3 and exited 0 with nothing on stderr. The sanitizer runs
the wrong code and says nothing. So the only instrument for the class is the
compiler, and the library now compiles with `-fstrict-aliasing
-Wstrict-aliasing=1` in `CFLAGS`.

Turning it on found eleven violations in `obj_load.c`, all one shape:
`obj_steal_into(&builder.faces, (void **)&obj->faces, ...)` stored a `void *`
through an lvalue whose declared type was `GMDL_Obj_Face *`. The fix was for
the helper to return the buffer, so the conversion happens in an ordinary
assignment, which is what the rule permits.

Three things about this are worth keeping, because each is a way the check
can be present and mean nothing:

- **The level matters.** `-Wall` turns on level 3, and level 3 reported none
  of the eleven. Levels 1 and 2 reported all eleven. Level 1 additionally
  reports the minimal pair above, which level 2 does not. The library is
  clean at all three, so it sits at the noisiest.
- **`-fstrict-aliasing` is what arms the warning, and gcc only enables it
  from -O2.** Measured: the warning fires at `-O0 -fstrict-aliasing` and is
  silent at both `-O0` and `-O1`. An optimised build is therefore not
  automatically a checked one, and naming the flag is what keeps the check
  live in the coverage tree, in a debug build, and in a sanitizer tree if one
  is ever pinned to -O1. Naming it changes what is checked rather than what
  is built: all nine objects of an -O0 tree are instruction-identical with
  and without it.
- **It was seen to fail.** Reinstating one of the eleven spellings fails all
  four trees `CFLAGS` reaches - release, asan, coverage and debug - with
  `error: dereferencing type-punned pointer` under `-Werror`. The last two
  are the -O0 ones, which is the evidence that naming `-fstrict-aliasing` did
  what it is there for. Building the debug tree takes one flag, since
  `BRANCH` becomes `-debug` and the dependency's pkg-config name is derived
  from it: `make BUILD=debug CUTIL_PC=ghoti.io-cutil-0 test` runs the whole
  suite at -O0 against the ordinary release prefix, 262 passing.

What the check does *not* establish is that the old code was miscompiled. It
was not, measurably: building `obj_load.c` at -O2 with `-fstrict-aliasing`
and with `-fno-strict-aliasing` gave the same instructions in a different
order - identical opcode multiset - and the suite's 262 tests produced
byte-identical output under both. The eleven were latent, which is the
ordinary state of this class and the reason a compile-time gate is worth more
here than another runtime one.

**What the fuzzers structurally cannot find.** They drive the caps that
exist, so a quantity with no field in the format's options is invisible to them -
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

- **Free-form geometry: evaluating it.** *Recording* it is settled and built
  - 3.19 holds all fifteen directives as of 2026-09-24. *Evaluating* it is
  not, and is the question that was actually behind 3.14: nothing here
  tessellates a surface or walks a trimming loop, and a polygon-mesh consumer
  like `libs/cjelly` wants neither. Recording it costs a consumer nothing and
  loses nothing; evaluating it is a different library.
- **A map directive with no path.** `GMDL_ERR_FORMAT` by default.
  `GMDL_Mtl_Options.accept_map_without_path` skips the line, which is what
  both references do. The default is the stricter reading; the field is the
  other one, and leaving it zero changes nothing (4.5).
- **A `v` line with fewer than three numbers.** `GMDL_ERR_FORMAT` by default.
  `GMDL_Obj_Options.accept_short_vertex` pads the missing coordinates with
  zero, which is what Blender and VTK do with `v 0 1`. A line with no
  numbers stays `GMDL_ERR_FORMAT` either way.
- **Extra face fields.** Ignored by default.
  `GMDL_Obj_Options.reject_extra_face_field` makes `1/2/3/4`
  `GMDL_ERR_FORMAT` (3.5).
- **`nan` and `inf`.** Recorded by default, on both parsers.
  `reject_non_finite` on the format's options makes them `GMDL_ERR_FORMAT`.
  `freecad` omits a non-finite `v` and does not give it an index, which is
  what FreeCAD 1.0 does. `non_finite_becomes_zero` stores 0 instead, which
  is what Blender 4.3 and VTK 9.3 do. `reject_non_finite` wins when both
  are set.
- **A leading byte-order mark.** Stripped by default, which is VTK.
  `keep_byte_order_mark` leaves it, which is Blender. Blender also drops
  the face that still names the hidden vertex; that is
  `omit_unresolved_faces`.
- **Comment and continuation.** The comment is cut first by default, which
  is VTK, so `# note \` does not swallow the next line.
  `join_before_comment` is Blender's order and does swallow it.
  `reject_face_comment` fails the file on `f 1 2 3 # tri`, which is VTK.
- **Continuation of `v` and of `g`.** Joined by default, which is Blender:
  a continued `v` is one vertex, and the face after `g a\` is lost.
  `reject_vertex_continuation` fails that `v`, and
  `break_group_continuation` keeps that face. Both are VTK.
- **FreeCAD's mesh.** `GMDL_Obj_Options.freecad` is that reader's polygon
  behaviour, measured on FreeCAD 1.0.0: no line rewriting, a bad face
  omitted rather than fatal, quads split, faces of five or more corners
  dropped, and `g a\` a format error. The default stays the specification.
  FreeCAD's reported point count leaves out vertices no facet uses; the
  model still holds those vertices.
- **Which exporters' spellings are still missing.** `map_Bump` and `map_refl`
  were found by asking Blender about 27 candidate spellings, not by reading
  the reference - the reference does not list them. Blender is one exporter;
  Maya, 3ds Max and Substance have their own habits, and the only honest way
  to know is to survey files they produce rather than to reason about what
  they ought to write. Until then the accepted set is "what was measured",
  which is a smaller claim than "what exists".

**No instrument refuses an allocation in a large MTL document.** Half of
this is closed and half is not, so it is worth saying which half.

`test_allocator.cpp` refuses every allocation against one designed document
per format, and the fuzz harnesses refuse one allocation against every corpus
document - so document shape is varied and allocation position is swept, but
never both at once, and never above a few kilobytes. A planted defect reachable only past 128
vertices survived a corpus replay and 1,982,029 fuzz executions, and died
instantly under the unit sweep.

The reason is narrower than "the corpus is too small", which was the earlier
claim here and is measurably wrong. Of 11,522 OBJ corpus documents, 9,054
parse; the largest reaches 160 vertices, 16 exceed 128 - the largest initial
capacity - and 56 exceed 32. So the corpus does reach the sizes where arrays
grow, just rarely. What it does not do is sweep them: the harness refuses one
allocation per document, and for a large document the allocations that matter
are the repeated growths near the end, which one arbitrary position is
unlikely to be.

So closing it wants allocation positions swept against a second, larger
document shape - not a bigger corpus. The unit sweep already forces growth on
every array (`kGrow` exceeds every initial capacity, and `check-lists.py`
holds it there); what was missing is a second shape beside the one designed
document per format.

**Done for OBJ; for MTL it was never open.** `regrow_obj()` is the same
shape as `rich_obj()` at 600 entries, past three doublings of the largest
initial capacity, so every array in it regrows and a refusal lands on second
and third growths as well as first ones - 770 (site, context) pairs that the
smaller sweep does not contain.

The version of this paragraph written on 2026-09-23 went on to say that MTL
had the same gap and wanted a `regrow_mtl()` beside `regrow_obj()`. **That was
wrong, and wrong for a reason worth keeping**: it carried a measurement from
OBJ to MTL across the fact that decides it. The OBJ gap exists because
`kGrow` is 160 against a largest initial capacity of 128, so the biggest
arrays grow exactly once. MTL has one array, `materials`, and its initial
capacity is **8** - so growth at 1.5x puts a reallocation at 9, 13, 19, 28,
41, 61, 91 and 136 materials, and `rich_mtl()`'s 160 crosses all eight. The
sweep refuses each of that parse's 21 allocations in turn, so second through
eighth growths are already in its population. Measured directly by counting
allocations per material count and reading where the count steps, rather than
inferred from the growth constant.

A `regrow_mtl()` at 600 would add three further growth positions (203, 303,
454) and nothing else, since a map path is a plain copy and not an array. It
is not worth a second full sweep, and saying so is the point: the work the
previous paragraph asked for would have been done against a gap that was not
there.
