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
malformed and is `GMDL_ERR_FORMAT`. A `curv` line is a directive this library
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
The full set of ignored OBJ directives is: `vp`, `cstype`, `deg`, `bmat`,
`step`, `p`, `l`, `curv`, `curv2`, `surf`, `parm`, `trim`, `hole`, `scrv`,
`sp`, `end`, `con`, `s`, `mg`, `bevel`, `c_interp`, `d_interp`, `lod`,
`shadow_obj`, `trace_obj`, `ctech`, `stech`, and anything else not listed in
section 3. Free-form geometry is out of scope permanently; smoothing groups
(`s`), lines (`l`) and points (`p`) are open questions (section 12).

---

## 3. OBJ

### 3.1 `v x y z [w]`

A vertex. Three numbers are required; a fourth (`w`) is accepted and
discarded. Fewer than three is `GMDL_ERR_FORMAT`. Text after the numbers is
ignored.

### 3.2 `vt u v [w]`

A texture coordinate. Two numbers required; a third accepted and discarded.

### 3.3 `vn x y z`

A normal. Three numbers required. Not normalised by the parser.

### 3.4 Numbers

A number is whatever `strtof` accepts: optional sign, decimal or exponent
form, and also `nan`, `inf` and hexadecimal floats. The parser records them
as written. A consumer that cannot draw a `nan` checks for it (section 8).

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
to the end of the file. `o` is treated identically to `g`; the distinction
the documentation draws (object versus group) is not preserved (12).

Only the first name is kept. The documentation permits `g a b c` to put the
following faces in three groups at once; this library records `a` (12).

A bare `g` names the group `default`, as the documentation specifies.

A group's `start_face` and `face_count` describe a contiguous range. Faces
before the first group belong to no group. A group with no faces is retained
with `face_count` 0.

### 3.7 `usemtl name`

Sets the material for the faces that follow. The name is required; a bare
`usemtl` is `GMDL_ERR_FORMAT`. Only the first token is the name.

Materials are not resolved by the OBJ parser. Each distinct name is assigned
an index in order of first use, recorded in `material_mappings`, and faces
carry that index. A repeated name reuses its index. The consumer resolves
names against an MTL library with `gmdl_mtl_find()`.

### 3.8 `mtllib path`

Names the material library. The documentation allows several paths on one
line; this library keeps the **first token of the last `mtllib` line** (12),
so a path containing a space is cut at the space. A bare `mtllib` clears it.

### 3.9 Names and paths

Group, object and material names are at most `GMDL_OBJ_MAX_NAME_LENGTH - 1`
(127) bytes; the `mtllib` path at most `GMDL_OBJ_MAX_PATH_LENGTH - 1` (255).
A longer one is `GMDL_ERR_LIMIT`.

### 3.10 The result

`GMDL_Obj` holds nine arrays, each `NULL` when its count is zero:

| Array | Element | Notes |
| --- | --- | --- |
| `vertices` | `x y z` floats | in file order |
| `texcoords` | `u v` | |
| `normals` | `x y z` | |
| `faces` | `GMDL_Obj_Face` | 0-based indices, `-1` for absent; `overflow` past four; `material_index`; `smoothing_group` |
| `lines` | `start`, `count` into `line_vertices` | one entry per `l` statement |
| `line_vertices` | vertex and texcoord index | every polyline's references, in file order |
| `points` | vertex index | one entry per index any `p` named |
| `groups` | name, `start_face`, `face_count` | in file order; ranges are contiguous and do not overlap |
| `material_mappings` | name, `index` | `index` equals position |

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
`off` nor a number is `GMDL_ERR_FORMAT`.

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

### 3.13 Not read

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

A line whose directive is none of the above and none of 3.1-3.12 is skipped,
which is how a file carrying an exporter's private extension still loads.

---

## 4. MTL

### 4.1 `newmtl name`

Begins a material. The name is required (`GMDL_ERR_FORMAT` without one) and
is the first token. Duplicate names are retained as separate materials;
`gmdl_mtl_find()` returns the first.

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
| `map_bump path`, `bump path` | `map_bump` |
| `map_Pr path` | `map_Pr` |
| `map_Pm path` | `map_Pm` |
| `map_Ps path` | `map_Ps` |
| `norm path` | `norm` |
| `disp path` | `disp` |
| `decal path` | `decal` |

`refl` is a map too, and has a shape of its own; it is 4.6.

`bump` and `map_bump` are two spellings of one property and share a field.
The dump writes `map_bump`, so the round trip is of the material rather than
of the keyword that set it.

**The path is the whole of the rest of the line**, with trailing blanks
removed, so `map_Kd my tex.png` names one file called `my tex.png`. The
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

`-type` is read where `-o` and `-s` are refused (4.5) because it is not the
same kind of thing. It says which slot the path belongs in; dropping it would
lose which of seven surfaces the file meant, where dropping a sampling option
loses only how the image is sampled. **Any other option is still
`GMDL_ERR_UNSUPPORTED`**, including one that follows a valid `-type`, so this
is not a general opening of the option syntax. A `-type` naming none of the
seven is `GMDL_ERR_FORMAT`.

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
| `max_faces` | 0 | `f`, and `l` and `p` records |
| `max_face_indices` | 0 | vertices in one face, or in one `l` |
| `max_groups` | 0 | `g` and `o` together |
| `max_materials` | 0 | distinct `usemtl` names in OBJ; `newmtl` in MTL |

`0` means no limit. When a record would take a count from `limit` to
`limit + 1`, the result is `GMDL_ERR_LIMIT` and parsing stops. `NULL` limits
mean the defaults. Only the line cap has a default because the input's size
already bounds the record counts, and a legitimate model can be very large;
set the others for untrusted input.

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
- a texture map path naming anything at all.

That last one is the only entry here that is a security question rather than
a correctness one. A map path is a string from the file, unresolved and
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
round-trips every float exactly.

**The OBJ output is read correctly by other tools.** Blender 4.3 and VTK 9.3
were both given this library's dumps of the checked-in models and of
synthetic cases, and both produced geometry identical to what they read from
the sources - the `%.9g` exponent forms, `1.00000001e-07` and
`-3.40282347e+38` among them. That is a measured result rather than an
intention; `notes/model/obj-differential.md` in the workspace has the method.

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

The dump writes `usemtl` when the material changes between consecutive faces,
`s` when the smoothing group does, `g` for each group before its faces, and
relative indices as absolute ones. Polylines and points follow the faces,
since no group covers them.

**Every write is checked, and every one of those checks is exercised.** Both
dumpers are mostly error handling by line count, and none of it had ever run:
a test that dumps to a file that works cannot reach a single one of those
arms. The tests hand over a stream that accepts a set number of writes and
refuses the rest, then sweep that number from zero upwards, so the failure
walks through the whole of a dump one position at a time. A model with groups
and one without are both swept, because they leave by different branches.
`GMDL_ERR_IO` is the only answer any position may give.

Three things a `GMDL_OK` model may hold cannot be written back, because the
format has no spelling for them rather than because the dumper is wrong: a
material no face uses, a face index below -1, and a name - or a texture map
path - ending in a backslash. Section 10 says what each one is and how the
fuzzers account for it.

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
coordinate value, every group name and span, the `mtllib` path, each face's
material by name and smoothing group, each polyline's span, and - with the
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

Three things are outside the comparison, because the format cannot express
them rather than because the dumper is wrong.

- **A material no face uses.** The dumper writes `usemtl` only where the
  material changes between faces, so a mapping created by a `usemtl` line
  that no face follows is never written. `material_mapping_count` is
  therefore not stable; each face's material *is*, and is compared by name.
- **A name, or a texture map path, ending in a backslash.** A name is
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

All three exemptions were measured rather than assumed: over the accumulated
corpus they account for every disagreement, and the invariant as stated
holds on all of it.

---

## 11. Implementation status

Nothing known. Every defect this section has listed is fixed and pinned by a
test; the shortfalls that remain are absences rather than misbehaviour, and
section 12 is where they are written down.

---

## 12. Open questions

- **Multiple `mtllib` paths and paths with spaces.** Keep a list, or keep
  one path including spaces? Either is a change to `GMDL_Obj`.
- **Multiple group names per `g` line.** The documentation allows `g a b`;
  supporting it means a face can be in several groups, which the contiguous
  range model cannot express.
- **`o` versus `g`.** Currently identical. A flag on `GMDL_Obj_Group` would
  preserve the distinction at no cost.
- **Free-form geometry.** `curv`, `surf` and the rest of the sub-language in
  3.13. A second data model rather than more fields, so it is a decision
  about what this library is for.
- **`shadow_obj` and `trace_obj`.** The two render attributes carrying real
  data - a path each. Recording them means deciding where per-state
  attributes live, which 3.13 explains is the blocker for all nine.
- **Texture map options.** `-o`, `-s`, `-clamp`, `-bm` and the rest are
  `GMDL_ERR_UNSUPPORTED` today (4.5). Implementing them means a place to put
  them and a decision about `-bm`, which Blender applies and VTK 9.3 does not
  parse at all.
- **A map directive with no path.** `GMDL_ERR_FORMAT` here, ignored by both
  references. Strictness is defensible and this is the one place 4.5 takes it
  further than either.
- **Extra face fields.** Reject `1/2/3/4`, or keep ignoring it?
- **`nan` and `inf`.** Record faithfully (current) or reject at parse time?
  Section 1 argues for faithful; a stricter mode via `GMDL_Limits` is an
  option that changes no default.
- **Vertex colours.** `v x y z r g b` is a common extension; the three extra
  numbers are currently discarded under 3.1.
