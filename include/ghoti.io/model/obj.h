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
 * @brief Which approximation directive a ::GMDL_Obj_Freeform_Attr holds.
 */
typedef enum GMDL_Obj_Freeform_Attr_Kind {
  GMDL_OBJ_FREEFORM_CTECH = 0, ///< `ctech technique resolution...`.
  GMDL_OBJ_FREEFORM_STECH,     ///< `stech technique resolution...`.
  GMDL_OBJ_FREEFORM_MG,        ///< `mg group res`, or `mg off`.
} GMDL_Obj_Freeform_Attr_Kind;

/**
 * @brief A `ctech`, `stech` or `mg` line, kept as text.
 *
 * These three are state for the **free-form** sub-language: `ctech` and
 * `stech` set how a curve or a surface is approximated, and `mg` sets the
 * merging group for the free-form surfaces that follow. This library does
 * not read free-form geometry (3.14), so there is nothing here for them to
 * apply to - which is why they are text and not parsed fields.
 *
 * That is deliberate and it is **provisional**. Parsing them into typed
 * records now would mean choosing a representation before the model they
 * describe exists, and attaching it to nothing. Keeping the line loses no
 * bytes and commits to nothing; when free-form geometry arrives, these get a
 * typed home beside it.
 *
 * `text` is everything after the directive with trailing blanks removed,
 * exactly as written. Unlike ::GMDL_Obj_Statement this names no file and no
 * command - there is nothing here a consumer could execute - so the warnings
 * on that type do not apply.
 */
typedef struct {
  GMDL_Obj_Freeform_Attr_Kind kind; ///< Which directive this was.
  char * text; ///< The text after it, owned by the ::GMDL_Obj.
} GMDL_Obj_Freeform_Attr;

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
 * `cstype`, `deg`, `bmat` and `step` are state in the file the way `usemtl`
 * is: each applies to every free-form element after it until the next one
 * changes it. Together they say how the control points a `curv`, `curv2` or
 * `surf` names are to be interpreted, and without them the element is a list
 * of indices that means nothing.
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
 * @brief A free-form curve or surface (`curv`, `curv2` or `surf`), recorded
 * and not evaluated.
 *
 * **Nothing in this library tessellates one.** The control points, the
 * parameter range and the state that says how to read them are all kept;
 * turning that into triangles is the consumer's, and it is a different
 * project from not discarding the patch. Section 3.19 says why the line is
 * drawn there.
 *
 * `end` closes an element in the file. It is not recorded, because a closed
 * element and an unclosed one hold the same data - the only thing `end`
 * decides is which element a body statement belongs to, and that question is
 * answered by the time parsing finishes.
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
  int32_t material_index; ///< Index into the material mappings, or -1.
  int32_t map_index;      ///< Index into the map mappings, or -1.
  int32_t render_index;   ///< Index into the render states, or -1.
} GMDL_Obj_Freeform;

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
   * this array too, in the format; this library does not read it yet (3.19).
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
   * `call` and `csh` statements, in file order, or NULL.
   *
   * Recorded, never executed - see ::GMDL_Obj_Statement. Their position
   * relative to the geometry is not kept, because nothing else in this model
   * is ordered against the geometry either.
   */
  GMDL_Obj_Statement * statements;
  size_t statement_count; ///< Number of statements.

  /**
   * `ctech`, `stech` and `mg` lines, in file order, or NULL.
   *
   * Kept as text because the geometry they describe is not read - see
   * ::GMDL_Obj_Freeform_Attr, which also says why that is provisional.
   */
  GMDL_Obj_Freeform_Attr * freeform_attrs;
  size_t freeform_attr_count; ///< Number of those lines.

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
 * @brief Parse an OBJ file from a stream.
 *
 * @param stream Stream positioned at the start of the OBJ data.
 * @param limits Parsing caps, or NULL for gmdl_limits_default().
 * @param allocator Allocator for the result, or NULL for the default.
 * @param out_obj Receives the parsed model on success.
 * @return GMDL_OK, or GMDL_ERR_FORMAT, GMDL_ERR_LIMIT, GMDL_ERR_OOM, or
 *   GMDL_ERR_INVALID.
 */
GMDL_API GMDL_Result gmdl_obj_load(GMDL_Stream * stream,
    const GMDL_Limits * limits, const GMDL_Allocator * allocator,
    GMDL_Obj ** out_obj);

/**
 * @brief Parse an OBJ file from a path.
 *
 * Equivalent to opening a stream on the file and calling gmdl_obj_load().
 *
 * @param path Path to the OBJ file.
 * @param limits Parsing caps, or NULL for gmdl_limits_default().
 * @param allocator Allocator for the result, or NULL for the default.
 * @param out_obj Receives the parsed model on success.
 * @return GMDL_OK, GMDL_ERR_IO if the file cannot be read, or any result
 *   gmdl_obj_load() can return.
 */
GMDL_API GMDL_Result gmdl_obj_load_file(const char * path,
    const GMDL_Limits * limits, const GMDL_Allocator * allocator,
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

#ifdef __cplusplus
}
#endif

#endif // GHOTI_IO_GMDL_OBJ_H
