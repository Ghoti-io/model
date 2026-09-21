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

A UTF-8 byte-order mark at the start of the input is skipped (11.1).

### 2.2 Line endings

A line ends at `\n`, at `\r`, or at `\r\n`, which counts as one ending. Mixed
endings in one file are fine. The final line need not be terminated.

### 2.3 Length

A line may be at most `max_line_length` bytes, not counting its ending. A
longer line is `GMDL_ERR_LIMIT` and parsing stops. A line of exactly the cap
is accepted.

### 2.4 Comments

`#` begins a comment that runs to the end of the line, anywhere on the line
(11.2). A line that is blank, or only a comment, is ignored.

### 2.5 Whitespace

Space and tab separate tokens. Any run of either is one separator. Leading
whitespace before the directive is permitted and ignored (11.3). Commas are
not separators: `v 1,2,3` is malformed.

### 2.6 Continuation

A `\` as the last non-blank character joins the next line to this one
(11.4). The joined line is subject to `max_line_length` as a whole.

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
by junk - is `GMDL_ERR_FORMAT` (11.5).

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
A longer one is `GMDL_ERR_LIMIT` (11.6).

### 3.10 The result

`GMDL_Obj` holds six arrays, each `NULL` when its count is zero:

| Array | Element | Notes |
| --- | --- | --- |
| `vertices` | `x y z` floats | in file order |
| `texcoords` | `u v` | |
| `normals` | `x y z` | |
| `faces` | `GMDL_Obj_Face` | 0-based indices, `-1` for absent; `overflow` past four; `material_index` |
| `groups` | name, `start_face`, `face_count` | in file order; ranges are contiguous and do not overlap |
| `material_mappings` | name, `index` | `index` equals position |

plus `mtllib` and the allocator that owns it all. Everything is freed by
`gmdl_obj_free()`, including every face's `overflow`.

---

## 4. MTL

### 4.1 `newmtl name`

Begins a material. The name is required (`GMDL_ERR_FORMAT` without one) and
is the first token. Duplicate names are retained as separate materials;
`gmdl_mtl_find()` returns the first.

Properties before any `newmtl` have nothing to apply to and are ignored.

### 4.2 Properties

Each applies to the current material. Trailing text after the expected
values is ignored. Every property is optional; an absent one leaves zero.

| Property | Values | Field |
| --- | --- | --- |
| `Ka r g b` | ambient colour | `Ka` |
| `Kd r g b` | diffuse colour | `Kd` |
| `Ks r g b` | specular colour | `Ks` |
| `Ns n` | specular exponent | `Ns` |
| `d n` | dissolve; 1 is opaque | `d` |
| `illum n` | illumination model, integer | `illum` |

The colour properties have three documented forms. `K? r g b` is the one
implemented. `K? r` - one value, meaning `r r r` - is accepted and expanded
(11.7). `K? xyz ...` (CIE XYZ) and `K? spectral file [factor]` are
`GMDL_ERR_UNSUPPORTED` (11.7). `d -halo n` is `GMDL_ERR_UNSUPPORTED` (11.7).

A property whose values do not parse - `Kd 0.5 x`, `illum x` - is
`GMDL_ERR_FORMAT`.

### 4.3 Ignored

`Ni`, `Tr`, `Ke`, `Tf`, `sharpness`, `map_Ka`, `map_Kd`, `map_Ks`, `map_Ns`,
`map_d`, `map_bump`, `bump`, `disp`, `decal`, `refl`, and any PBR extension
(`Pr`, `Pm`, `Ps`, `Pc`, `Pcr`, `aniso`, `anisor`, `norm`, `Ke`). Texture
maps are the largest omission and the first thing a real renderer will want
(section 12).

### 4.4 The result

`GMDL_Mtl` holds `materials`, `material_count` and the allocator.
`gmdl_mtl_find(mtl, name)` is a linear search by exact name.

---

## 5. Limits

`GMDL_Limits`, with `gmdl_limits_default()`:

| Field | Default | Counts |
| --- | --- | --- |
| `max_line_length` | 65536 | bytes in one line, excluding its ending |
| `max_vertices` | 0 (unlimited) | `v` records |
| `max_texcoords` | 0 | `vt` |
| `max_normals` | 0 | `vn` |
| `max_faces` | 0 | `f` |
| `max_face_indices` | 0 | vertices in one face |
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
| `GMDL_ERR_IO` | `_file` could not open or read the path |
| `GMDL_ERR_FORMAT` | a line was malformed (sections 3 and 4) |
| `GMDL_ERR_UNSUPPORTED` | a documented form this library does not implement (4.2) |
| `GMDL_ERR_LIMIT` | a `GMDL_Limits` cap was exceeded, or a name or path was too long |
| `GMDL_ERR_OOM` | the allocator returned `NULL` |

On any failure `*out` is `NULL` and nothing is allocated for the caller. The
file is read to the first error and no further; there is no partial result.

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
- a `material_index` whose name no MTL library defines.

A consumer indexes nothing without a range check. cjelly's
`cjelly_model_mesh_from_obj()` skips a face on any of the first two and counts
it in `dropped_faces`; that is the pattern.

---

## 9. Dump

`gmdl_obj_dump()` and `gmdl_mtl_dump()` write a model back out as text, for
debugging and for tests. The guarantee is **structural round-trip**: parsing
the dump yields a model with the same counts, the same indices, the same
names, the same group ranges and the same material assignments. Floats are
written with `%f`, so a value survives to six decimal places only (11.8).

The dump writes `usemtl` when the material changes between consecutive faces,
`g` for each group before its faces, and relative indices as absolute ones.

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

The invariant the harness checks: whatever the result, the parser neither
crashes nor leaks, and a `GMDL_OK` model survives a dump and reload.

---

## 11. Implementation status

Reproductions use `gmdl_obj_load()` on the given bytes.

1. **A UTF-8 BOM swallows the first line.** `\xEF\xBB\xBFv 1 2 3` yields no
   vertex, because the directive does not match at byte 0. Fix: skip the BOM
   before the first line.

2. **Comments are not stripped, so a trailing comment on a face line becomes
   face vertices.** `f 1 2 3 # c` yields a five-vertex face whose last two
   vertices are `-1`. On `v` lines the comment is harmless only because
   `sscanf` stops after three numbers. Fix: cut each line at its first `#`
   before dispatching.

3. **Leading whitespace makes a line invisible.** `  v 1 2 3` is silently
   ignored, in both parsers. `  newmtl a` on the first line of an MTL file
   yields zero materials.

4. **Line continuation is not implemented.** `f 1 2 3 \` followed by ` 2`
   yields a four-vertex face whose fourth vertex is `-1` (the backslash is a
   token) and the next line is dropped under 11.3.

5. **Malformed face tokens are accepted.** `f a b c` yields a face of three
   `-1` vertices; `f 1x 1y 1z` and `f 1.5 1 1` yield `1`s. Section 3.5 says
   `GMDL_ERR_FORMAT`.

6. **Over-long names are truncated silently** to 127 bytes (`g`, `o`,
   `usemtl`, `newmtl`) and paths to 255 (`mtllib`). Section 3.9 says
   `GMDL_ERR_LIMIT`.

7. **The one-value colour form and the unsupported forms are all
   `GMDL_ERR_FORMAT`.** `Kd 0.5` should expand to `0.5 0.5 0.5`; `Kd xyz 1 1 1`,
   `Kd spectral f.rfl` and `d -halo 0.5` should be `GMDL_ERR_UNSUPPORTED`.

8. **Dump loses float precision.** `%f` writes six decimals; `v 0.0000001 0 0`
   round-trips to `0`. `%.9g` round-trips every float exactly.

9. **Dump drops faces that precede the first group.** When a model has any
   groups, `gmdl_obj_dump()` writes only the faces inside group ranges. For
   `v 1 2 3`, `f 1 1 1`, `g later`, face 0 belongs to no group and is not
   written, so the dump does not round-trip.

10. **A fourth face field is ignored.** `f 1/2/3/4` yields `1/2/3`. Whether
    that is right is an open question (12); today it is at least undocumented.

---

## 12. Open questions

- **Multiple `mtllib` paths and paths with spaces.** Keep a list, or keep
  one path including spaces? Either is a change to `GMDL_Obj`.
- **Multiple group names per `g` line.** The documentation allows `g a b`;
  supporting it means a face can be in several groups, which the contiguous
  range model cannot express.
- **`o` versus `g`.** Currently identical. A flag on `GMDL_Obj_Group` would
  preserve the distinction at no cost.
- **Smoothing groups (`s`), lines (`l`) and points (`p`).** Ignored today.
  Smoothing groups matter for normal generation, which cjelly does; lines and
  points matter for CAD-style models.
- **Texture maps in MTL.** `map_Kd` at minimum. Needs a path field and a
  decision on the `-o`, `-s`, `-clamp` options.
- **`Tr` as `1 - d`.** Some exporters write only `Tr`. Map it when `d` is
  absent?
- **Extra face fields.** Reject `1/2/3/4`, or keep ignoring it?
- **`nan` and `inf`.** Record faithfully (current) or reject at parse time?
  Section 1 argues for faithful; a stricter mode via `GMDL_Limits` is an
  option that changes no default.
- **Vertex colours.** `v x y z r g b` is a common extension; the three extra
  numbers are currently discarded under 3.1.
