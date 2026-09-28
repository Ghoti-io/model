/*
 * SPDX-License-Identifier: LGPL-3.0-only
 *
 * Copyright (C) 2026 Corey Pennycuff
 *
 * This file is part of Ghoti.io Model.
 *
 * Ghoti.io Model is free software: you can redistribute it and/or modify it
 * under the terms of the GNU Lesser General Public License version 3 as
 * published by the Free Software Foundation.
 *
 * Ghoti.io Model is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
 * or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU Lesser General Public
 * License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

/**
 * @file
 *
 * Wavefront OBJ geometry parsing.
 *
 * Reference documents:
 *   https://en.wikipedia.org/wiki/Wavefront_.obj_file
 *   https://paulbourke.net/dataformats/obj/
 *   https://paulbourke.net/dataformats/obj/obj_spec.pdf
 */

#ifndef GHOTI_IO_GMDL_OBJ_H
#define GHOTI_IO_GMDL_OBJ_H

#include <ghoti.io/model/core.h>
#include <ghoti.io/model/macros.h>
#include <ghoti.io/model/stream.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Longest group, object, or material name retained, including the NUL. */
#define GMDL_OBJ_MAX_NAME_LENGTH 128

/** Longest `mtllib` path retained, including the NUL. */
#define GMDL_OBJ_MAX_PATH_LENGTH 256

/**
 * @brief One `mtllib` path.
 *
 * A struct rather than a bare array so the model can hold a contiguous list
 * of them the way it holds every other record.
 */
typedef struct {
  char path[GMDL_OBJ_MAX_PATH_LENGTH]; ///< The path, whole line, NUL-terminated.
} GMDL_Obj_Mtllib;

/**
 * @brief One `maplib` path.
 *
 * The same shape as ::GMDL_Obj_Mtllib and a separate type on purpose: the
 * two lists name different kinds of library, and a function that took either
 * would let a caller hand it the wrong one and get no complaint.
 */
typedef struct {
  char path[GMDL_OBJ_MAX_PATH_LENGTH]; ///< The path, whole line, NUL-terminated.
} GMDL_Obj_Maplib;

/**
 * @brief The render attributes in force, as one record elements name.
 *
 * `bevel`, `c_interp`, `d_interp` and `lod` are state in the file exactly as
 * `usemtl` is: each applies to every element after it until the next one
 * changes it. They describe how a renderer should draw the geometry and
 * change no geometry.
 *
 * They are held as a **record elements point at** rather than as four fields
 * on every element, which is what section 3.14 objected to when it declined
 * them. A document that never mentions them holds no records and every
 * element carries -1; one that toggles `bevel` holds two.
 *
 * Measured on x86-64: the index lands in padding ::GMDL_Obj_Face already
 * carried, so a face stays 80 bytes and a point stays 16. A polyline grows
 * from 24 to 32, which is the one place this costs anything - and there is
 * one polyline per `l` statement against one face per `f`. Four fields
 * instead would have widened all three. Another attribute of this kind goes
 * in this record without widening any element again.
 *
 * Values are recorded as written, not validated. `lod` is documented as 0 to
 * 100 and a file saying `lod 200` keeps 200, for the reason
 * ::GMDL_Obj_Color gives for a colour outside `[0, 1]`: deciding what an
 * out-of-range value means is a consumer's job, and a parser that clamped it
 * would make the file unrecoverable.
 */
typedef struct {
  bool bevel;    ///< `bevel on`; false is the format's default.
  bool c_interp; ///< `c_interp on`; false is the format's default.
  bool d_interp; ///< `d_interp on`; false is the format's default.
  int32_t lod;   ///< `lod level`, as written; 0 is the format's default.
} GMDL_Obj_Render_State;

/**
 * @brief One `shadow_obj` or `trace_obj` path.
 *
 * Both name an **OBJ file** - the same kind of thing - which is why they
 * share a type where ::GMDL_Obj_Mtllib and ::GMDL_Obj_Maplib do not: those
 * two name libraries of definitions in two different formats. The type says
 * what the path points at.
 *
 * Nothing here opens the file. A consumer that does must treat the path the
 * way section 8 says to treat a texture map path, and more carefully: this
 * one names a document that would then be parsed.
 */
typedef struct {
  char path[GMDL_OBJ_MAX_PATH_LENGTH]; ///< The path, whole line, NUL-terminated.
} GMDL_Obj_Render_Object;

/**
 * @brief A 3D vertex.
 */
typedef struct {
  float x; ///< X coordinate.
  float y; ///< Y coordinate.
  float z; ///< Z coordinate.
} GMDL_Obj_Vertex;

/**
 * @brief A vertex colour, from the `r g b` extension to `v` (3.1).
 *
 * Recorded exactly as written: not clamped, not converted out of whatever
 * colour space the writer had in mind, and not rejected for being outside
 * `[0, 1]`. Blender reads `1.5` as sRGB and hands its renderer `2.537`;
 * deciding that is a consumer's job, and a parser that did it would make the
 * file unrecoverable.
 */
typedef struct {
  float r;      ///< Red, as written.
  float g;      ///< Green, as written.
  float b;      ///< Blue, as written.
  bool present; ///< False when this vertex's `v` line carried no colour.
} GMDL_Obj_Color;

/**
 * @brief The homogeneous weight on a `v` line (3.1).
 *
 * The specification's fourth number, which a rational curve multiplies its
 * control point by. Kept beside the vertex rather than in it, for the reason
 * ::GMDL_Obj_Color is: a polygon file never writes one, and widening every
 * vertex to hold it would make that file pay for a curve it does not have.
 *
 * `w` is 1 when the line carried no weight, which is the value that leaves a
 * point unweighted. ::present says which, because a file may write `w` as 1
 * and an absent weight is also 1 - the same collision a colour has with
 * white. Four or five numbers on the line are a weight (3.1). Six or more
 * are the colour extension, which occupies those fields instead, so a vertex
 * from a parse carries one or the other and not both.
 */
typedef struct {
  float w;      ///< The weight, or 1 when the line carried none.
  bool present; ///< True when the line carried a fourth number as a weight.
} GMDL_Obj_Weight;

/**
 * @brief A 2D texture coordinate.
 */
typedef struct {
  float u; ///< U coordinate.
  float v; ///< V coordinate.
} GMDL_Obj_TexCoord;

/**
 * @brief A vertex normal.
 */
typedef struct {
  float x; ///< X component.
  float y; ///< Y component.
  float z; ///< Z component.
} GMDL_Obj_Normal;

/**
 * @brief A point in parameter space, from `vp u [v] [w]` (3.19).
 *
 * The control points of the free-form sub-language, and a different kind of
 * thing from ::GMDL_Obj_Vertex: these live in the parameter space of a curve
 * or a surface rather than in the model's own space, which is why the format
 * gives them their own directive and their own index space. `curv2` indexes
 * this array; `curv` and `surf` index ::GMDL_Obj.vertices.
 *
 * **`count` is part of the record, not a parsing detail.** A `vp` naming one
 * number is a point on a curve and one naming two is a point on a surface,
 * and the file says which by how many it wrote. Without the count, `vp 0.5`
 * and `vp 0.5 0` are the same record and the dump has to guess which
 * statement to write back.
 *
 * The coordinates a line did not carry hold the format's own defaults - 0
 * for `v` and 1 for `w`, the weight that leaves a rational point unweighted
 * - so a consumer that ignores `count` still reads something meaningful
 * rather than whatever was in the allocation.
 */
typedef struct {
  float u;       ///< First coordinate; every `vp` line carries one.
  float v;       ///< Second, or 0 when the line carried only `u`.
  float w;       ///< Weight, or 1 when the line did not carry one.
  int32_t count; ///< How many numbers the line carried: 1, 2 or 3.
} GMDL_Obj_Param_Vertex;

/**
 * @brief One vertex of a face beyond the fourth.
 *
 * Nearly every face in an OBJ file is a triangle or a quad, so the first four
 * vertices live in the face itself and anything further goes here.
 */
typedef struct {
  int32_t vertex;   ///< Vertex index (0-based).
  int32_t texcoord; ///< Texture coordinate index (0-based), or -1 if absent.
  int32_t normal;   ///< Normal index (0-based), or -1 if absent.
} GMDL_Obj_Face_Overflow;

/**
 * @brief How face indices are reported.
 *
 * Indices are 0-based here, whatever the file wrote. OBJ indices are 1-based,
 * and a negative one is relative - -1 names the most recently declared element
 * of that kind. The parser resolves both forms, measuring a relative index
 * from the counts at that point in the file, which is what the specification
 * asks for and cannot be reconstructed afterwards from the totals.
 *
 * A file may still name an element that does not exist. That is not treated as
 * a reason to reject the file, since readers differ on how to handle it, so a
 * consumer that indexes the arrays directly must range check first.
 */

/**
 * @brief A face.
 *
 * `count` is the total number of vertices in the face. The first four are in
 * the fixed arrays; the remaining `count - 4` are in `overflow`.
 */
typedef struct {
  int32_t vertex[4];   ///< Vertex indices (0-based).
  int32_t texcoord[4]; ///< Texture coordinate indices, or -1 if absent.
  int32_t normal[4];   ///< Normal indices, or -1 if absent.
  size_t count;        ///< Number of vertices in the face.
  int32_t material_index; ///< Index into the material mappings, or -1.
  /**
   * Index into the map mappings (`usemap`), or -1 for none.
   *
   * State exactly as `usemtl` is, with one difference that matters to the
   * dump: -1 is a value the file can *write*. `usemap off` turns texture
   * mapping off, so an element naming no map after one that named a map has
   * an OBJ spelling, which is not true of materials (3.15, 9).
   */
  int32_t map_index;
  /**
   * Index into the render states, or -1 when every attribute is at its
   * default.
   *
   * -1 is not "unset": it names the state a file starts in, so an element
   * carrying it is fully described. See ::GMDL_Obj_Render_State.
   */
  int32_t render_index;
  /**
   * Smoothing group in force for this face, or 0 for none.
   *
   * `s` is state, like `usemtl`: it applies to every face after it until the
   * next one. It is recorded per face rather than as a run, because that is
   * the question a consumer generating normals actually asks - whether these
   * two faces share one - and a run would make it compute the answer.
   * `s off` and `s 0` both mean 0, which is the format's own default.
   */
  int32_t smoothing_group;
  GMDL_Obj_Face_Overflow * overflow; ///< Vertices past the fourth, or NULL.
} GMDL_Obj_Face;

/**
 * @brief One vertex reference of a polyline (`l`).
 *
 * A line carries no normals - the format gives it `v` and an optional `vt`.
 */
typedef struct {
  int32_t vertex;   ///< Vertex index (0-based).
  int32_t texcoord; ///< Texture coordinate index (0-based), or -1 if absent.
} GMDL_Obj_Line_Vertex;

/**
 * @brief A polyline (`l`), as a range of `line_vertices`.
 *
 * One `l` statement is one polyline of any length, so the references live in
 * a flat array and each line names its span - the same shape `GMDL_Obj_Group`
 * uses over faces. A face hides its first four in the element itself because
 * nearly every face is a triangle or a quad; a polyline has no such typical
 * length, so there is nothing to special-case.
 */
typedef struct {
  size_t start; ///< Index of this line's first entry in `line_vertices`.
  size_t count; ///< Number of entries.
  int32_t material_index; ///< Index into the material mappings, or -1.
  int32_t map_index;      ///< Index into the map mappings, or -1.
  int32_t render_index;   ///< Index into the render states, or -1.
} GMDL_Obj_Line;

/**
 * @brief A point element (`p`).
 *
 * A vertex and the material in force when it was declared. `usemtl` applies
 * to `p` and `l` exactly as it does to `f`, so all three carry one.
 */
typedef struct {
  int32_t vertex;         ///< Vertex index (0-based).
  int32_t material_index; ///< Index into the material mappings, or -1.
  int32_t map_index;      ///< Index into the map mappings, or -1.
  int32_t render_index;   ///< Index into the render states, or -1.
} GMDL_Obj_Point;

/**
 * @brief A group (`g`) or object (`o`), naming a run of faces.
 */
typedef struct {
  char name[GMDL_OBJ_MAX_NAME_LENGTH]; ///< Group or object name.
  size_t start_face; ///< Index of the first face in the group.
  size_t face_count; ///< Number of faces in the group.
  /**
   * True when the file said `o`, false when it said `g`.
   *
   * The two behave identically here - each starts a contiguous run of faces -
   * but they do not mean the same thing to the tools that write them, and a
   * reader that flattens them cannot put the distinction back. Blender is
   * explicit about it: `o` names the *object* the faces become, `g` names a
   * vertex group inside one. Recorded rather than acted on, so the dump can
   * write back the spelling the file used.
   */
  bool is_object;
  /**
   * True when this name was not the first on its `g` line.
   *
   * `g a b` is two groups that share one face range (3.6). The dump writes
   * them as one line; two `g` lines would hand the faces to only the second
   * group on the way back in. An `o` is never joined: the specification
   * gives an object one name, and that name may contain spaces.
   */
  bool joined;
} GMDL_Obj_Group;

/**
 * @brief The association between a `usemtl` name and the index faces use.
 */
typedef struct {
  char name[GMDL_OBJ_MAX_NAME_LENGTH]; ///< Material name.
  int32_t index;                       ///< Index assigned to the material.
} GMDL_Obj_Material_Mapping;

/**
 * @brief The association between a `usemap` name and the index elements use.
 *
 * The same shape ::GMDL_Obj_Material_Mapping has, because `usemap` is the
 * same kind of directive as `usemtl`: a name, in force until the next one,
 * resolved by the consumer against a library this parser does not open
 * (3.15). Kept as its own type for the reason ::GMDL_Obj_Maplib is.
 */
typedef struct {
  char name[GMDL_OBJ_MAX_NAME_LENGTH]; ///< Texture map name.
  int32_t index;                       ///< Index assigned to the map.
} GMDL_Obj_Map_Mapping;

/**
 * @brief Which general statement a ::GMDL_Obj_Statement holds.
 */
typedef enum GMDL_Obj_Statement_Kind {
  GMDL_OBJ_STATEMENT_CALL = 0, ///< `call filename [args]`.
  GMDL_OBJ_STATEMENT_CSH,      ///< `csh command` or `csh -command`.
} GMDL_Obj_Statement_Kind;

/**
 * @brief A `call` or `csh` statement, recorded and never acted on.
 *
 * These two ask the parser to do something rather than describe geometry:
 * `call` pulls in another `.obj`, and `csh` runs a shell command. **This
 * library does neither.** It records what the file said and hands it over,
 * because a parser that executed a command found in its input would make
 * every `.obj` a program, and the decision to run one belongs to the caller
 * that knows where the file came from.
 *
 * `text` is everything after the directive with trailing blanks removed,
 * exactly as written - for `csh` including the leading `-` that means "ignore
 * the exit status", and for `call` the filename and any arguments together,
 * unsplit and unresolved. A caller acting on either must treat it the way
 * section 8 says to treat a texture map path, and more carefully: a `call`
 * names a file that would then be parsed, and a `csh` names a command.
 */
typedef struct {
  GMDL_Obj_Statement_Kind kind; ///< Which directive this was.
  char * text; ///< The text after it, owned by the ::GMDL_Obj.
} GMDL_Obj_Statement;

/**
 * @brief How a `ctech` line asks a curve to be approximated (3.18).
 *
 * `ctech` and `stech` are the same idea for the two kinds of element, and
 * they are **two enumerations rather than one** because the format gives
 * them different technique names: a curve is subdivided by `cparm` and a
 * surface by `cparma` or `cparmb`, and no file may write `ctech cparma` or
 * `stech cparm`. One shared enumeration would make both of those spellable
 * and leave the dump free to write a line no reader accepts.
 *
 * The technique also fixes how many numbers the line carries, so there is no
 * count beside it: see ::GMDL_Obj_Freeform_State.ctech_value.
 */
typedef enum GMDL_Obj_Ctech {
  GMDL_OBJ_CTECH_NONE = 0, ///< No `ctech` was in force.
  GMDL_OBJ_CTECH_CPARM,    ///< `ctech cparm res`; constant parametric.
  GMDL_OBJ_CTECH_CSPACE,   ///< `ctech cspace maxlength`; constant spatial.
  GMDL_OBJ_CTECH_CURV,     ///< `ctech curv maxdist maxangle`; curvature.
} GMDL_Obj_Ctech;

/**
 * @brief How an `stech` line asks a surface to be approximated (3.18).
 *
 * See ::GMDL_Obj_Ctech for why the two are separate enumerations.
 */
typedef enum GMDL_Obj_Stech {
  GMDL_OBJ_STECH_NONE = 0, ///< No `stech` was in force.
  GMDL_OBJ_STECH_CPARMA,   ///< `stech cparma ures vres`; separate u and v.
  GMDL_OBJ_STECH_CPARMB,   ///< `stech cparmb uvres`; one resolution.
  GMDL_OBJ_STECH_CSPACE,   ///< `stech cspace maxlength`; constant spatial.
  GMDL_OBJ_STECH_CURV,     ///< `stech curv maxdist maxangle`; curvature.
} GMDL_Obj_Stech;

/**
 * @brief Whether an `mg` line was in force, and whether it turned on (3.18).
 *
 * Three states rather than a `bool`, because "the file said `mg off`" and
 * "the file said nothing" are different documents and a dump that wrote
 * `mg off` for the second would put a directive into a file that had none.
 * That is the distinction ::GMDL_OBJ_CSTYPE_NONE draws for `cstype`.
 */
typedef enum GMDL_Obj_Merge {
  GMDL_OBJ_MERGE_NONE = 0, ///< No `mg` was in force.
  GMDL_OBJ_MERGE_OFF,      ///< `mg off`.
  GMDL_OBJ_MERGE_ON,       ///< `mg group [res]`.
} GMDL_Obj_Merge;

/**
 * @brief Which basis a `cstype` line named (3.19).
 */
typedef enum GMDL_Obj_Cstype {
  GMDL_OBJ_CSTYPE_NONE = 0, ///< No `cstype` was in force.
  GMDL_OBJ_CSTYPE_BMATRIX,  ///< `cstype bmatrix`; `bmat` supplies the basis.
  GMDL_OBJ_CSTYPE_BEZIER,   ///< `cstype bezier`.
  GMDL_OBJ_CSTYPE_BSPLINE,  ///< `cstype bspline`.
  GMDL_OBJ_CSTYPE_CARDINAL, ///< `cstype cardinal`.
  GMDL_OBJ_CSTYPE_TAYLOR,   ///< `cstype taylor`.
} GMDL_Obj_Cstype;

/**
 * @brief The free-form state in force where a curve or a surface began.
 *
 * `cstype`, `deg`, `bmat`, `step`, `ctech`, `stech` and `mg` are state in the
 * file the way `usemtl` is: each applies to every free-form element after it
 * until the next one changes it. The first four say how the control points a
 * `curv`, `curv2` or `surf` names are to be interpreted, and without them the
 * element is a list of indices that means nothing. The last three say how
 * finely the result is to be approximated and how adjacent surfaces meet
 * (3.18); a consumer that only reads the geometry can ignore them, which is
 * why they are last and why an element that names none is still complete.
 *
 * **Held on the element rather than indexed**, which is the opposite of what
 * ::GMDL_Obj_Render_State does, for a reason that is about counts and not
 * about taste: there is one face per `f` and a document has millions of
 * them, so four fields on a face would be four fields a document pays for
 * everywhere. There is one ::GMDL_Obj_Freeform per patch and a document has
 * dozens. An index here would buy a few bytes and cost a cap, a linear
 * search and an indirection every consumer would have to follow.
 *
 * **`deg` and `step` record how many numbers their line carried**, in
 * ::degree_count and ::step_count, rather than marking an absent one with a
 * sentinel value. This is the shape ::GMDL_Obj_Param_Vertex uses and for a
 * sharper version of the same reason: -1 was the sentinel here at first, and
 * `step -1 1` is a line a file can write, so the dump read a real step as
 * "none in force", wrote nothing, and the reload lost the second number. The
 * fuzzer found it in ten minutes. A count cannot collide with a value.
 *
 * A count of 0 is "the file set none". A conforming document states `cstype`
 * and `deg` before any element, so an element carrying ::GMDL_OBJ_CSTYPE_NONE
 * or a degree count of 0 came from a file that did not - recorded rather than
 * refused, because the specification's reading and what exporters emit have
 * not been measured against each other here.
 */
typedef struct {
  GMDL_Obj_Cstype type; ///< From `cstype`, or ::GMDL_OBJ_CSTYPE_NONE.
  /**
   * The `rat` prefix on `cstype`, making the curve or surface rational.
   *
   * Kept separately from ::type because it is orthogonal to the basis: the
   * format writes `cstype rat bspline`, not a sixth basis name, and folding
   * the two into one enumeration would make "rational" unspellable for a
   * basis nobody has written a rational example of.
   */
  bool rational;
  int32_t degree_u; ///< `deg`'s first number, as written.
  int32_t degree_v; ///< `deg`'s second, as written; 0 when it carried one.
  int32_t degree_count; ///< Numbers on the `deg` line: 0 (none), 1 or 2.
  int32_t step_u;   ///< `step`'s first number, as written.
  int32_t step_v;   ///< `step`'s second, as written; 0 when it carried one.
  int32_t step_count; ///< Numbers on the `step` line: 0 (none), 1 or 2.
  size_t basis_u_start; ///< First `bmat u` value in ::GMDL_Obj.basis_values.
  size_t basis_u_count; ///< How many, or 0 when no `bmat u` was in force.
  size_t basis_v_start; ///< First `bmat v` value in ::GMDL_Obj.basis_values.
  size_t basis_v_count; ///< How many, or 0 when no `bmat v` was in force.
  GMDL_Obj_Ctech ctech; ///< From `ctech`, or ::GMDL_OBJ_CTECH_NONE.
  /**
   * The numbers the `ctech` line carried, in the order it wrote them.
   *
   * **The technique says how many are meaningful**, so there is no count
   * beside it the way ::degree_count stands beside ::degree_u: `cparm` and
   * `cspace` carry one, `curv` carries two, and a line with the wrong number
   * of them is ::GMDL_ERR_FORMAT rather than a record with a gap in it. That
   * is the difference from `deg` and `step`, where the format itself defines
   * both a one-number and a two-number form and a reader cannot tell an
   * absent second number from a written one without counting.
   *
   * | ::ctech | `[0]` | `[1]` |
   * | --- | --- | --- |
   * | ::GMDL_OBJ_CTECH_CPARM | `res` | unused |
   * | ::GMDL_OBJ_CTECH_CSPACE | `maxlength` | unused |
   * | ::GMDL_OBJ_CTECH_CURV | `maxdist` | `maxangle` |
   *
   * Unused entries are 0. They are not named individually because no name
   * fits all three rows: `[0]` is a parametric resolution, a length and a
   * distance depending on which technique is in force.
   */
  float ctech_value[2];
  GMDL_Obj_Stech stech; ///< From `stech`, or ::GMDL_OBJ_STECH_NONE.
  /**
   * The numbers the `stech` line carried. See ::ctech_value.
   *
   * | ::stech | `[0]` | `[1]` |
   * | --- | --- | --- |
   * | ::GMDL_OBJ_STECH_CPARMA | `ures` | `vres` |
   * | ::GMDL_OBJ_STECH_CPARMB | `uvres` | unused |
   * | ::GMDL_OBJ_STECH_CSPACE | `maxlength` | unused |
   * | ::GMDL_OBJ_STECH_CURV | `maxdist` | `maxangle` |
   */
  float stech_value[2];
  GMDL_Obj_Merge merge; ///< From `mg`, or ::GMDL_OBJ_MERGE_NONE.
  /**
   * The merging group number, when ::merge is ::GMDL_OBJ_MERGE_ON.
   *
   * **A group of 0 is the format's other spelling of "off"**, alongside the
   * `off` keyword, and this records which one the file wrote rather than
   * folding them together - the same call `g` and `o` get. A consumer acting
   * on the state reads 0 and `off` alike; one writing the file back out
   * writes what it was given.
   */
  int32_t merge_group;
  float merge_resolution; ///< `mg`'s second number, or 0 when it had one.
  /**
   * Numbers on the `mg` line: 0 (for `off` or no line), 1 or 2.
   *
   * The specification writes `mg group res`, and also says a group of 0
   * turns merging off - for which a resolution means nothing and files write
   * none. So one number is accepted and recorded as one, for the reason
   * ::degree_count exists: an absent `res` and a written `res 0` are
   * different lines and no float value can stand for "absent".
   */
  int32_t merge_count;
} GMDL_Obj_Freeform_State;

/**
 * @brief Which free-form element a ::GMDL_Obj_Freeform holds.
 */
typedef enum GMDL_Obj_Freeform_Kind {
  GMDL_OBJ_CURVE = 0, ///< `curv u0 u1 v1 v2 ...`, a curve in model space.
  GMDL_OBJ_CURVE2,    ///< `curv2 vp1 vp2 ...`, a curve in parameter space.
  GMDL_OBJ_SURFACE,   ///< `surf s0 s1 t0 t1 v1/vt1/vn1 ...`.
} GMDL_Obj_Freeform_Kind;

/**
 * @brief One control-point reference of a free-form element.
 *
 * **Which array `vertex` indexes depends on the element's kind**, and that is
 * the format's doing rather than this library's: ::GMDL_OBJ_CURVE and
 * ::GMDL_OBJ_SURFACE name ::GMDL_Obj.vertices, while ::GMDL_OBJ_CURVE2 names
 * ::GMDL_Obj.param_vertices. Both numberings start at 1 in the file, so a
 * consumer that read the wrong array would get a real point every time and
 * never be told.
 *
 * Only `surf` writes `texcoord` and `normal`; the other two kinds leave both
 * at -1, because the format gives their references no such syntax.
 */
typedef struct {
  int32_t vertex;   ///< Control point index (0-based), in the kind's array.
  int32_t texcoord; ///< Texture coordinate index, or -1.
  int32_t normal;   ///< Normal index, or -1.
} GMDL_Obj_Freeform_Vertex;

/**
 * @brief A stretch of one `curv2`, as `trim`, `hole`, `scrv` and `con` name it.
 *
 * All four of those directives reference a curve the same way - a parameter
 * range and the curve's index - so they share one record rather than four
 * that would drift apart.
 *
 * **`curve2d` counts `curv2` elements, not ::GMDL_Obj.freeforms entries.**
 * The format numbers free-form elements within their own kind, the way it
 * numbers `v` separately from `vt`: `trim 0 1 2` names the file's second
 * `curv2`, whatever else stands between them. ::GMDL_Obj.freeforms holds all
 * three kinds in file order, so the two numbers differ as soon as a document
 * mixes kinds, and ::gmdl_obj_freeform_of_kind() is the way across.
 *
 * Resolving it here instead would mean refusing a forward reference, which
 * the format allows: an index counts from the start of the file and nothing
 * says the curve it names has been read yet. So this holds the ordinal for
 * the same reason ::GMDL_Obj_Face holds an out-of-range vertex index - the
 * range check is the consumer's (section 1), and an index that cannot be
 * resolved yet is not an index that is wrong.
 */
typedef struct {
  /**
   * The start and end parameter values, the format's `u0` and `u1`.
   *
   * Named as the specification names them rather than `start` and `end`,
   * because every other `start` in this header is the first index of a span
   * and these are neither indices nor a span.
   */
  float u0;
  float u1;         ///< See ::u0.
  int32_t curve2d; ///< 0-based ordinal among the file's `curv2` elements.
} GMDL_Obj_Curve_Ref;

/**
 * @brief Which body statement a ::GMDL_Obj_Freeform_Body holds (3.19).
 *
 * The kind also says which array the body's span indexes, because the five
 * directives carry three different payloads. See ::GMDL_Obj_Freeform_Body.
 */
typedef enum GMDL_Obj_Body_Kind {
  GMDL_OBJ_BODY_PARM_U = 0, ///< `parm u p1 p2 ...`; floats.
  GMDL_OBJ_BODY_PARM_V,     ///< `parm v p1 p2 ...`; floats.
  GMDL_OBJ_BODY_TRIM,       ///< `trim u0 u1 curv2d ...`; curve references.
  GMDL_OBJ_BODY_HOLE,       ///< `hole u0 u1 curv2d ...`; curve references.
  GMDL_OBJ_BODY_SCRV,       ///< `scrv u0 u1 curv2d ...`; curve references.
  GMDL_OBJ_BODY_SP,         ///< `sp vp1 vp2 ...`; `vp` indices.
} GMDL_Obj_Body_Kind;

/**
 * @brief One body statement of a free-form element (3.19).
 *
 * `parm`, `trim`, `hole`, `scrv` and `sp` stand between a `curv`, `curv2` or
 * `surf` and its `end`, and describe the element they stand in. Each line
 * becomes one of these, in file order, and ::GMDL_Obj_Freeform names the
 * span of them that belongs to it.
 *
 * **One record per line, not one span per directive**, which matters for
 * three of the five: each `trim` builds a *separate* outer trimming loop,
 * and so does each `hole` and each `scrv`. Merging two `trim` lines into one
 * list of curve references would join two loops into one and change the
 * shape the file describes, while still round-tripping through this
 * library's own dump - the kind of loss that agrees with itself.
 *
 * ::kind says which array ::start and ::count index:
 *
 * | kind | array |
 * | --- | --- |
 * | ::GMDL_OBJ_BODY_PARM_U, ::GMDL_OBJ_BODY_PARM_V | ::GMDL_Obj.parm_values |
 * | ::GMDL_OBJ_BODY_TRIM, ::GMDL_OBJ_BODY_HOLE, ::GMDL_OBJ_BODY_SCRV | ::GMDL_Obj.curve_refs |
 * | ::GMDL_OBJ_BODY_SP | ::GMDL_Obj.special_points |
 */
typedef struct {
  GMDL_Obj_Body_Kind kind; ///< Which directive this was.
  size_t start; ///< First entry, in the array ::kind selects.
  size_t count; ///< How many entries.
} GMDL_Obj_Freeform_Body;

/**
 * @brief A free-form curve or surface (`curv`, `curv2` or `surf`), recorded
 * and not evaluated.
 *
 * **Nothing in this library tessellates one.** The control points, the
 * parameter range and the state that says how to read them are all kept;
 * turning that into triangles is the consumer's, and it is a different
 * project from not discarding the patch. Section 3.19 says why the line is
 * drawn there.
 *
 * `end` closes an element in the file. It is not recorded as a field,
 * because a closed element and an unclosed one hold the same data - what
 * `end` decides is which element a body statement belongs to, and that is
 * answered here, in ::body_start and ::body_count, by the time parsing
 * finishes.
 */
typedef struct {
  GMDL_Obj_Freeform_Kind kind; ///< Which directive declared it.
  GMDL_Obj_Freeform_State state; ///< The state in force where it began.
  /**
   * The parameter range, whose meaning follows ::kind.
   *
   * ::GMDL_OBJ_CURVE writes `u0` and `u1` into the first two and leaves the
   * rest at zero; ::GMDL_OBJ_SURFACE writes `s0`, `s1`, `t0` and `t1` into
   * all four; ::GMDL_OBJ_CURVE2 has no range and leaves all four at zero.
   * One array rather than six named fields, because two of any naming would
   * always be meaningless and a reader would have to know the kind either
   * way.
   */
  float range[4];
  size_t start; ///< Index of its first entry in ::GMDL_Obj.freeform_vertices.
  size_t count; ///< Number of entries.
  /**
   * Its body statements, as a span of ::GMDL_Obj.freeform_bodies.
   *
   * In file order, across all five directives together rather than grouped
   * by kind, because `trim` and `hole` interleave to describe a surface with
   * holes in it and the order they were written in is the order they have to
   * be written back in.
   *
   * ::body_count is 0 for an element whose file gave it no body - which is
   * every element in a document that states only `curv` and `end`, and is
   * what an element looked like before 3.19 read these at all.
   */
  size_t body_start;
  size_t body_count; ///< Number of body statements.
  int32_t material_index; ///< Index into the material mappings, or -1.
  int32_t map_index;      ///< Index into the map mappings, or -1.
  int32_t render_index;   ///< Index into the render states, or -1.
} GMDL_Obj_Freeform;

/**
 * @brief One end of a `con` line: a surface, and a curve in its space.
 */
typedef struct {
  int32_t surface;          ///< 0-based ordinal among the file's `surf`s.
  GMDL_Obj_Curve_Ref curve; ///< The `q0 q1 curv2d` that follows it.
} GMDL_Obj_Connection_End;

/**
 * @brief A `con` line: two surfaces joined along a curve (3.19).
 *
 * `con surf_1 q0_1 q1_1 curv2d_1 surf_2 q0_2 q1_2 curv2d_2` says that two
 * surfaces meet, and which curve in each one's parameter space they meet
 * along. It is the one free-form directive that is neither state nor a body
 * statement: it stands at file level and names its surfaces, so it is kept
 * in its own array rather than on an element.
 *
 * Both ordinals count within their own kind, for the reason
 * ::GMDL_Obj_Curve_Ref gives; ::gmdl_obj_freeform_of_kind() resolves them.
 * Recorded and not acted on - nothing here checks that the two surfaces
 * exist, that the curves lie in their parameter spaces, or that the join is
 * geometrically possible.
 */
typedef struct {
  GMDL_Obj_Connection_End a; ///< The first surface and curve.
  GMDL_Obj_Connection_End b; ///< The second.
} GMDL_Obj_Connection;

/**
 * @brief A parsed OBJ file.
 */
typedef struct {
  GMDL_Obj_Vertex * vertices; ///< Vertices, or NULL when there are none.
  size_t vertex_count;        ///< Number of vertices.

  /**
   * Vertex colours, or NULL when no `v` line in the file carried one.
   *
   * When it is not NULL it has exactly ::vertex_count entries, so it is
   * indexed by the same subscript as ::vertices. A file may colour some
   * vertices and not others - Blender discards the colours of such a file
   * entirely - and the entries for the uncoloured ones have `present` false
   * and hold white, which is the value that changes nothing when a consumer
   * multiplies by it. Keeping the array parallel rather than widening
   * ::GMDL_Obj_Vertex means a file without colours pays nothing for them.
   */
  GMDL_Obj_Color * colors;
  size_t color_count; ///< Number of colours: 0, or ::vertex_count.

  /**
   * Homogeneous weights, or NULL when no `v` line carried one.
   *
   * The same shape as ::colors: when it is not NULL it has exactly
   * ::vertex_count entries, indexed by vertex number, and an entry whose
   * `present` is false holds 1. See ::GMDL_Obj_Weight.
   */
  GMDL_Obj_Weight * weights;
  size_t weight_count; ///< Number of weights: 0, or ::vertex_count.

  GMDL_Obj_TexCoord * texcoords; ///< Texture coordinates, or NULL.
  size_t texcoord_count;         ///< Number of texture coordinates.

  GMDL_Obj_Normal * normals; ///< Normals, or NULL.
  size_t normal_count;       ///< Number of normals.

  /**
   * Parameter-space control points (`vp`), or NULL when the file named none.
   *
   * Their own index space, separate from ::vertices: `curv2` counts into
   * this array and `curv` and `surf` count into that one, so a file carrying
   * both has two independent numberings and a reader that merged them would
   * resolve every free-form reference to the wrong point. `sp` counts into
   * this array too, whatever kind of element it belongs to; see
   * ::GMDL_Obj.special_points.
   */
  GMDL_Obj_Param_Vertex * param_vertices;
  size_t param_vertex_count; ///< Number of parameter-space points.

  GMDL_Obj_Face * faces; ///< Faces, or NULL.
  size_t face_count;     ///< Number of faces.

  GMDL_Obj_Line * lines; ///< Polylines (`l`), or NULL.
  size_t line_count;     ///< Number of polylines.

  GMDL_Obj_Line_Vertex * line_vertices; ///< Every polyline's references.
  size_t line_vertex_count;             ///< Number of those references.

  /**
   * Point elements (`p`), or NULL.
   *
   * One `p` statement declares one point per index it names, so unlike `l`
   * there is nothing to group: the statement boundary carries no meaning
   * that survives parsing.
   */
  GMDL_Obj_Point * points;
  size_t point_count; ///< Number of points.

  GMDL_Obj_Group * groups; ///< Groups and objects, or NULL.
  size_t group_count;      ///< Number of groups.

  GMDL_Obj_Material_Mapping * material_mappings; ///< Mappings, or NULL.
  size_t material_mapping_count;                 ///< Number of mappings.

  /**
   * `usemap` name-to-index mappings, or NULL when the file named no map.
   *
   * Assigned in order of first use, the way ::material_mappings are, and
   * read the same way: elements carry the index, the consumer resolves the
   * name. `usemap off` names no map and assigns no index.
   */
  GMDL_Obj_Map_Mapping * map_mappings;
  size_t map_mapping_count; ///< Number of map mappings.

  /**
   * Distinct render-attribute states the document put in force, or NULL.
   *
   * One entry per distinct combination, in order of first use, the way
   * ::material_mappings are assigned. The all-defaults state gets no entry:
   * elements name it with -1, so a document mentioning none of the four
   * directives holds no records at all.
   */
  GMDL_Obj_Render_State * render_states;
  size_t render_state_count; ///< Number of render states.

  /**
   * Free-form curves and surfaces (`curv`, `curv2`, `surf`), or NULL.
   *
   * Recorded and not evaluated - see ::GMDL_Obj_Freeform.
   */
  GMDL_Obj_Freeform * freeforms;
  size_t freeform_count; ///< Number of free-form elements.

  /**
   * Every free-form element's control-point references, in one flat array.
   *
   * Each element names its own span, the shape ::line_vertices already uses.
   * Which array an entry's `vertex` indexes depends on the element's kind:
   * see ::GMDL_Obj_Freeform_Vertex.
   */
  GMDL_Obj_Freeform_Vertex * freeform_vertices;
  size_t freeform_vertex_count; ///< Number of those references.

  /**
   * Every `bmat` line's values, in one flat array, or NULL.
   *
   * A basis matrix is `(deg + 1)` squared numbers and the degree is set by a
   * separate directive, so its length is not known from the directive alone
   * - which is why these live in a flat array with spans rather than in a
   * fixed field. ::GMDL_Obj_Freeform_State names the span in force.
   */
  float * basis_values;
  size_t basis_value_count; ///< Number of `bmat` values.

  /**
   * Every free-form element's body statements, in one flat array, or NULL.
   *
   * File order, with each element naming its own span - the shape
   * ::line_vertices and ::freeform_vertices already use. See
   * ::GMDL_Obj_Freeform_Body for which array a body's own span indexes.
   */
  GMDL_Obj_Freeform_Body * freeform_bodies;
  size_t freeform_body_count; ///< Number of body statements.

  /**
   * Every `parm` line's values, in one flat array, or NULL.
   *
   * A knot vector's length follows from the degree and the number of control
   * points, neither of which is on the `parm` line, so this is a flat array
   * with spans for the reason ::basis_values is.
   */
  float * parm_values;
  size_t parm_value_count; ///< Number of `parm` values.

  /**
   * Every `trim`, `hole` and `scrv` curve reference, or NULL.
   *
   * Those three name spans of this array. A ::GMDL_Obj_Connection holds its
   * two by value instead, because a `con` names exactly two and a span would
   * be a fixed length spelled as a variable one - so a `con` adds nothing
   * here and is capped by `max_connections` rather than by
   * `max_curve_refs`.
   */
  GMDL_Obj_Curve_Ref * curve_refs;
  size_t curve_ref_count; ///< Number of curve references.

  /**
   * Every `sp` line's `vp` indices, in one flat array, or NULL.
   *
   * 0-based, resolved the way every other index in this file is, and
   * counting into ::param_vertices - never ::vertices. A special point is a
   * point in parameter space whatever kind of element it belongs to, so
   * unlike a control-point reference this does not change array with the
   * element's kind.
   */
  int32_t * special_points;
  size_t special_point_count; ///< Number of special points.

  /**
   * `con` lines, in file order, or NULL.
   *
   * Recorded and not acted on - see ::GMDL_Obj_Connection.
   */
  GMDL_Obj_Connection * connections;
  size_t connection_count; ///< Number of connections.

  /**
   * `call` and `csh` statements, in file order, or NULL.
   *
   * Recorded, never executed - see ::GMDL_Obj_Statement. Their position
   * relative to the geometry is not kept, because nothing else in this model
   * is ordered against the geometry either.
   */
  GMDL_Obj_Statement * statements;
  size_t statement_count; ///< Number of statements.

  /**
   * Every `mtllib` path the document named, in the order it named them.
   *
   * A document may carry several `mtllib` lines and both references load
   * every one of them, so keeping only the last silently dropped material
   * libraries a file asked for. Each entry is a whole line including spaces
   * (3.8).
   */
  GMDL_Obj_Mtllib * mtllibs;
  size_t mtllib_count; ///< Number of `mtllib` paths.

  /**
   * The first `mtllib` path, or "" if the document named none.
   *
   * Equivalent to `mtllibs[0].path`, kept because a document naming one
   * library is the overwhelming case and reaching for it should not require
   * indexing. It held the *last* path before `mtllibs` existed, which
   * differs only for documents that were losing libraries anyway.
   */
  char mtllib[GMDL_OBJ_MAX_PATH_LENGTH];

  /**
   * Every `maplib` path the document named, in the order it named them.
   *
   * Read the way ::mtllibs is - the whole line is one path - and for the
   * same reason, which is this library's rule rather than a reference's:
   * measured 2026-09-23, Blender 4.3.2 does not implement `maplib` at all
   * and prints "OBJ element not recognized" for it (3.15). There is no
   * compatibility scalar here because this list had no predecessor.
   */
  GMDL_Obj_Maplib * maplibs;
  size_t maplib_count; ///< Number of `maplib` paths.

  /**
   * Every `shadow_obj` path the document named, in order, or NULL.
   *
   * The specification says one per file, and this is a list because a
   * document that carries two would otherwise lose one silently - which is
   * exactly the defect ::mtllib had. A conforming document gives this one
   * entry. Position relative to the geometry is not recorded, for the reason
   * ::statements gives.
   */
  GMDL_Obj_Render_Object * shadow_objs;
  size_t shadow_obj_count; ///< Number of `shadow_obj` paths.

  /** Every `trace_obj` path the document named, in order, or NULL. */
  GMDL_Obj_Render_Object * trace_objs;
  size_t trace_obj_count; ///< Number of `trace_obj` paths.

  const GMDL_Allocator * allocator; ///< Allocator that owns the arrays above.
} GMDL_Obj;

/**
 * @brief Caps and readings for one OBJ parse.
 *
 * Caps are the size_t fields and come first, so a test can tell a new cap
 * from a reading by where the bools begin. Zero for a cap means no cap,
 * except ::max_line_length, where zero means
 * ::GMDL_DEFAULT_MAX_LINE_LENGTH: the line buffer is allocated before the
 * first line is read, and a caller who zeroes this struct and sets only the
 * caps they care about must still get a bounded line.
 *
 * A reading's zero is the behaviour the specification states. Set the field
 * to take the other reading. Pass NULL to a load function for
 * gmdl_obj_options_default().
 */
typedef struct GMDL_Obj_Options {
  /**
   * Longest accepted line, in bytes, not counting its ending.
   *
   * Zero means ::GMDL_DEFAULT_MAX_LINE_LENGTH, not "unlimited".
   */
  size_t max_line_length;
  size_t max_vertices;       ///< Cap on `v` records.
  size_t max_texcoords;      ///< Cap on `vt` records.
  size_t max_normals;        ///< Cap on `vn` records.
  size_t max_param_vertices; ///< Cap on `vp` records.
  size_t max_faces;          ///< Cap on `f`, `l` and `p` elements together.
  size_t max_face_indices;   ///< Cap on references in one element.
  size_t max_groups;         ///< Cap on `g` and `o` records together.
  size_t max_materials;      ///< Cap on distinct `usemtl` names.
  size_t max_statements;     ///< Cap on `call` and `csh` records.
  size_t max_mtllibs;        ///< Cap on `mtllib` records.
  size_t max_maplibs;        ///< Cap on `maplib` records.
  size_t max_maps;           ///< Cap on distinct `usemap` names.
  size_t max_render_states;  ///< Cap on distinct render-attribute states.
  size_t max_shadow_objs;    ///< Cap on `shadow_obj` records.
  size_t max_trace_objs;     ///< Cap on `trace_obj` records.
  size_t max_freeforms;      ///< Cap on `curv`, `curv2` and `surf` together.
  size_t max_basis_values;   ///< Cap on `bmat` values, across every line.
  size_t max_freeform_bodies; ///< Cap on `parm`, `trim`, `hole`, `scrv` and
                              ///< `sp` lines, across every element.
  size_t max_parm_values;    ///< Cap on `parm` values, across every line.
  size_t max_curve_refs;     ///< Cap on curve references in `trim`, `hole`
                             ///< and `scrv`.
  size_t max_special_points; ///< Cap on `sp` indices, across every line.
  size_t max_connections;    ///< Cap on `con` records.

  /**
   * Accept `v` with one or two numbers, padding the missing ones with zero.
   *
   * Zero, the default, is ::GMDL_ERR_FORMAT. A line with no numbers stays
   * ::GMDL_ERR_FORMAT either way: there is no vertex to pad.
   */
  bool accept_short_vertex;
  /**
   * Reject a face, line or free-form reference with a fourth `/` field.
   *
   * Zero, the default, ignores that field (3.5).
   */
  bool reject_extra_face_field;
  /**
   * Reject `nan` and `inf` wherever a float is read.
   *
   * Zero, the default, records the value.
   */
  bool reject_non_finite;
  /**
   * Read polygons the way FreeCAD 1.0's ReaderOBJ does.
   *
   * Zero is the specification. Set, lines are not rewritten (2.1, 2.4, 2.5,
   * 2.6), a short or non-finite `v` is omitted and takes no index, a face
   * that does not parse or names a missing vertex is omitted, a face of five
   * or more corners is omitted, and a quad is two triangles, corners
   * (0, 1, 2) and (2, 3, 0). A `g` line ending in an unpaired `\` is
   * ::GMDL_ERR_FORMAT. Where this meets another reading, the element is
   * omitted rather than padded or rejected.
   */
  bool freecad;
} GMDL_Obj_Options;

/**
 * @brief Fill in the default OBJ options.
 *
 * @param options Structure to populate. NULL is ignored.
 */
GMDL_API void gmdl_obj_options_default(GMDL_Obj_Options * options);

/**
 * @brief Parse an OBJ file from a stream.
 *
 * @param stream Stream positioned at the start of the OBJ data.
 * @param options Caps and readings, or NULL for gmdl_obj_options_default().
 * @param allocator Allocator for the result, or NULL for the default.
 * @param out_obj Receives the parsed model on success.
 * @return GMDL_OK, or GMDL_ERR_FORMAT, GMDL_ERR_LIMIT, GMDL_ERR_OOM, or
 *   GMDL_ERR_INVALID.
 */
GMDL_API GMDL_Result gmdl_obj_load(GMDL_Stream * stream,
    const GMDL_Obj_Options * options, const GMDL_Allocator * allocator,
    GMDL_Obj ** out_obj);

/**
 * @brief Parse an OBJ file from a path.
 *
 * Equivalent to opening a stream on the file and calling gmdl_obj_load().
 *
 * @param path Path to the OBJ file.
 * @param options Caps and readings, or NULL for gmdl_obj_options_default().
 * @param allocator Allocator for the result, or NULL for the default.
 * @param out_obj Receives the parsed model on success.
 * @return GMDL_OK, GMDL_ERR_IO if the file cannot be read, or any result
 *   gmdl_obj_load() can return.
 */
GMDL_API GMDL_Result gmdl_obj_load_file(const char * path,
    const GMDL_Obj_Options * options, const GMDL_Allocator * allocator,
    GMDL_Obj ** out_obj);

/**
 * @brief Free a model returned by gmdl_obj_load(). NULL is ignored.
 *
 * @param obj The model.
 */
GMDL_API void gmdl_obj_free(GMDL_Obj * obj);

/**
 * @brief Write a model back out as OBJ text.
 *
 * @param obj The model.
 * @param fd Destination, e.g. stdout.
 * @return GMDL_OK, GMDL_ERR_INVALID, or GMDL_ERR_IO.
 */
GMDL_API GMDL_Result gmdl_obj_dump(const GMDL_Obj * obj, FILE * fd);

/**
 * @brief Find the nth free-form element of one kind.
 *
 * `trim`, `hole`, `scrv` and `con` name a `curv2` or a `surf` by its ordinal
 * *within its own kind*, which is how the format numbers everything - `vt`
 * 2 is the second `vt`, not the second line of the file. ::GMDL_Obj.freeforms
 * holds the three kinds together in file order, so the two numbers part
 * company as soon as a document mixes them, and every consumer of a
 * ::GMDL_Obj_Curve_Ref would otherwise write this loop.
 *
 * It is a search rather than a second index array because the population is
 * dozens of patches, not millions of faces: a parallel array would cost
 * every document memory so that trimmed surfaces - which few documents have
 * at all - could skip a walk.
 *
 * @param obj The model.
 * @param kind Which kind to count.
 * @param ordinal 0-based, as ::GMDL_Obj_Curve_Ref.curve2d holds it.
 * @return The element, or NULL if `obj` is NULL or there is no such one -
 *   which includes every negative ordinal and every forward reference the
 *   file made but never satisfied.
 */
GMDL_API const GMDL_Obj_Freeform * gmdl_obj_freeform_of_kind(
    const GMDL_Obj * obj, GMDL_Obj_Freeform_Kind kind, int32_t ordinal);

#ifdef __cplusplus
}
#endif

#endif // GHOTI_IO_GMDL_OBJ_H
