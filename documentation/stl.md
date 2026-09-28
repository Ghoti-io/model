# STL

There is no standards-body specification for STL. The behaviours below are
what this library records and writes. Where readers disagree, the default is
the ordinary spelling and the other readings are options.

## 1. Forms

STL is either ASCII text or a little-endian binary layout. One `.stl` file
uses one form. This library reads both. The dump writes binary unless
`GMDL_Stl_Options.write_ascii` is set.

### 1.1 Binary

- 80 header bytes. Readers usually ignore them. Materialise Magics may place
  `COLOR=` followed by four bytes of RGBA (0–255) in the header; that default
  colour is recorded when seen.
- A little-endian `uint32` triangle count.
- For each triangle: twelve little-endian `float32` values (normal `nx ny nz`,
  then three vertices) and a little-endian `uint16` attribute word.

A stream whose size is exactly `84 + 50 * n` for some `n` is taken as binary
when neither `force_ascii` nor `force_binary` is set, including when the
header begins with the letters `solid`.

### 1.2 ASCII

```
solid <name>
  facet normal ni nj nk
    outer loop
      vertex v1x v1y v1z
      vertex v2x v2y v2z
      vertex v3x v3y v3z
    endloop
  endfacet
endsolid [<name>]
```

Keywords are matched case-insensitively by default. Blender and OpenSCAD
reject mixed case (empty mesh); `require_lowercase_keywords` makes that
`GMDL_ERR_FORMAT`. The solid name is the rest of the `solid` line after the
keyword and one blank run; an empty name is allowed. There is no portable
colour spelling in ASCII.

### 1.3 Detection

1. `force_binary` wins over `force_ascii` when both are set.
2. Otherwise `force_ascii` selects ASCII.
3. Otherwise a known stream size that equals `84 + 50 * n` selects binary.
4. Otherwise a leading `solid` token (case-insensitive) selects ASCII.
5. Otherwise the bytes are parsed as binary.

## 2. Colour

The specification treats the attribute word as an attribute-byte count, almost
always zero. Two vendor conventions repurpose it as packed RGB. They disagree
on the same bits, so this library never guesses the convention from the word
alone.

| Convention | Valid bit | Channels (5 bits each) |
| --- | --- | --- |
| VisCAM / SolidView | bit 15 = 1 means live | R, G, B from high to low |
| Materialise Magics | bit 15 = 0 means live | B, G, R from high to low |

`GMDL_Stl_Options.color_convention` selects decode on load and pack on dump:

- `GMDL_STL_COLOR_NONE` (default): store `attribute` only.
- `GMDL_STL_COLOR_VISCAM` / `GMDL_STL_COLOR_MAGICS`: when the word is live
  under that convention, fill `r`, `g`, `b` in `[0, 1]` and set
  `GMDL_STL_TRI_HAS_COLOR`.

Magics `COLOR=` in the header is always recorded as `default_color` with
`GMDL_STL_HAS_DEFAULT_COLOR` when the four bytes are present after `COLOR=`.
Per-face unpacking still requires a non-none convention.

On a binary dump, a triangle with colour present and a non-none convention is
packed into the attribute word. Otherwise the stored `attribute` is written.
With Magics and a default colour present, `COLOR=` is written into the
header. An ASCII dump does not encode colour.

## 3. Numbers

Coordinates use `.` as the decimal separator regardless of locale. `nan` and
`inf` are recorded by default. `reject_non_finite` makes them
`GMDL_ERR_FORMAT`. A zero normal is kept as zero by default.
`recompute_zero_normals` replaces it with the unit normal of the triangle;
Blender, FreeCAD and OpenSCAD all do that, and their presets set the option.

## 4. Result model

`GMDL_Stl` holds the form that was loaded, the solid name (ASCII), the
eighty header bytes (binary), an optional default colour, and the triangles.
Vertices are not indexed: each facet carries its three corners.

## 5. Options

`GMDL_Stl_Options`, with `gmdl_stl_options_default()`:

| Field | Default | Counts / meaning |
| --- | --- | --- |
| `max_line_length` | `GMDL_DEFAULT_MAX_LINE_LENGTH` (65536) | bytes in one ASCII line, excluding its ending |
| `max_triangles` | 0 | facets |
| `write_ascii` | false | dump writes ASCII instead of binary |
| `force_ascii` | false | load parses as ASCII |
| `force_binary` | false | load parses as binary; wins over `force_ascii` |
| `reject_non_finite` | false | `nan` and `inf` are `GMDL_ERR_FORMAT` |
| `recompute_zero_normals` | false | replace an all-zero normal with the geometric unit normal |
| `require_lowercase_keywords` | false | ASCII keywords must be lowercase |

`color_convention` defaults to `GMDL_STL_COLOR_NONE`. It is an enum rather
than a bool, so it is not a row of the bool table above; section 2 is the
contract.

Named combinations that match a measured reader are
`gmdl_stl_options_blender()`, `gmdl_stl_options_freecad()`, and
`gmdl_stl_options_openscad()`. Those only write the individual reading
fields; the loader never asks for an oracle by name.

`0` means no limit for `max_triangles`. For `max_line_length`, `0` means
`GMDL_DEFAULT_MAX_LINE_LENGTH`.

## 6. Errors

| Code | When |
| --- | --- |
| `GMDL_ERR_FORMAT` | the bytes are not a well-formed STL under the selected form |
| `GMDL_ERR_LIMIT` | a cap was exceeded, or an ASCII line was too long |
| `GMDL_ERR_OOM` | the allocator returned NULL |
| `GMDL_ERR_IO` | a dump write failed, or a file could not be opened |
| `GMDL_ERR_INVALID` | a required argument was NULL |

## 7. Dump

Binary is the default spelling. Blender's exporter defaults to binary.
OpenSCAD's GUI defaults to binary. That is the ordinary interchange form.

ASCII is available for inspection and for pipelines that require text.
`write_ascii` selects it. Colour is not written in ASCII.
