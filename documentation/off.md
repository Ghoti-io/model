# OFF

Geomview's Object File Format. There is no standards-body specification.
The behaviours below are what this library records and writes. OpenSCAD
2021.01 and FreeCAD 1.0.0's Mesh module read the ordinary ASCII spelling;
Blender 4.3.2 in the pinned oracle image has no OFF importer.

## 1. Form

This library reads and writes **ASCII** OFF. Geomview's `BINARY` spelling,
`4OFF`, `nOFF`, and texture-coordinate (`ST…OFF`) prefixes are
`GMDL_ERR_UNSUPPORTED`.

```
OFF
NVertices NFaces NEdges
x y z
…
Nv v0 v1 … vNv-1 [colorspec]
…
```

The keyword may also be `COFF`, `NOFF` or `CNOFF` (colour and/or normal
prefixes). Counts may sit on the keyword line (`OFF 3 1 0`), which is what
OpenSCAD writes, or on the next line, which is what FreeCAD's Mesh reader
expects. FreeCAD 1.0.0 returns an empty mesh for the one-line spelling; the
dump always uses two lines so both reload it.

`NEdges` is recorded and written back; it is not checked against the faces.
Comments begin with `#` and run to end of line. Blank lines are skipped.

A missing keyword is `GMDL_ERR_FORMAT`. Geomview allows a bare counts line;
OpenSCAD refuses it and FreeCAD returns an empty mesh. Requiring the keyword
matches the tools that actually accept the file.

## 2. Vertices

Each vertex is `x y z`. With `N` in the keyword, `nx ny nz` follow. With `C`,
`r g b` or `r g b a` follow after that (normals first when both are present).
Colours are stored in `[0, 1]`. Three or four integers in `0..255` are scaled;
three or four floats are taken as already in `[0, 1]`.

## 3. Faces

Each face is `Nv` then `Nv` zero-based indices, then an optional colourspec
to end of line:

| colourspec | Meaning |
| --- | --- |
| (empty) | no colour |
| one integer | colormap index (`GMDL_OFF_FACE_HAS_COLOR_INDEX`) |
| 3 or 4 integers | RGB[A] in `0..255` |
| 3 or 4 floats | RGB[A] in `[0, 1]` |

A face with fewer than three corners is `GMDL_ERR_FORMAT`. An index outside
`0..NVertices-1` is `GMDL_ERR_FORMAT`.

## 4. Numbers

Coordinates use `.` as the decimal separator regardless of locale. `nan` and
`inf` are recorded by default. `reject_non_finite` makes them
`GMDL_ERR_FORMAT`.

## 5. Options

`GMDL_Off_Options`, with `gmdl_off_options_default()`:

| Field | Default | Counts / meaning |
| --- | --- | --- |
| `max_line_length` | `GMDL_DEFAULT_MAX_LINE_LENGTH` (65536) | bytes in one line, excluding its ending |
| `max_vertices` | 0 | vertices |
| `max_faces` | 0 | faces |
| `max_face_corners` | 0 | corners on one face |
| `reject_non_finite` | false | `nan` and `inf` are `GMDL_ERR_FORMAT` |

`0` means no limit for the size_t caps other than `max_line_length`, where
`0` means `GMDL_DEFAULT_MAX_LINE_LENGTH`.

No oracle preset is shipped yet: FreeCAD and OpenSCAD agreed on the cases
measured for the ordinary spelling, and Blender does not read OFF in the
pinned image. Measured disagreements would land here as fields and sugar
helpers, the same shape as STL.

## 6. Errors

| Code | When |
| --- | --- |
| `GMDL_ERR_FORMAT` | the bytes are not a well-formed ASCII OFF |
| `GMDL_ERR_UNSUPPORTED` | `BINARY`, `4OFF`, `nOFF`, or an `ST…` texture prefix |
| `GMDL_ERR_LIMIT` | a cap was exceeded, or a line was too long |
| `GMDL_ERR_OOM` | the allocator returned NULL |
| `GMDL_ERR_IO` | a dump write failed, or a file could not be opened |
| `GMDL_ERR_INVALID` | a required argument was NULL |

## 7. Dump

Always ASCII. Keyword from `present` (`OFF` / `COFF` / `NOFF` / `CNOFF`).
Counts on the following line. Face colours are written as floats in `[0, 1]`;
a colour index is written as one integer. Vertex colours and normals follow
the keyword. `NEdges` is written as stored (often `0`).
