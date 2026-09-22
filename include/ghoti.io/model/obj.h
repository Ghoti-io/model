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
 * @brief A 3D vertex.
 */
typedef struct {
  float x; ///< X coordinate.
  float y; ///< Y coordinate.
  float z; ///< Z coordinate.
} GMDL_Obj_Vertex;

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
} GMDL_Obj_Line;

/**
 * @brief A group (`g`) or object (`o`), naming a run of faces.
 */
typedef struct {
  char name[GMDL_OBJ_MAX_NAME_LENGTH]; ///< Group or object name.
  size_t start_face; ///< Index of the first face in the group.
  size_t face_count; ///< Number of faces in the group.
} GMDL_Obj_Group;

/**
 * @brief The association between a `usemtl` name and the index faces use.
 */
typedef struct {
  char name[GMDL_OBJ_MAX_NAME_LENGTH]; ///< Material name.
  int32_t index;                       ///< Index assigned to the material.
} GMDL_Obj_Material_Mapping;

/**
 * @brief A parsed OBJ file.
 */
typedef struct {
  GMDL_Obj_Vertex * vertices; ///< Vertices, or NULL when there are none.
  size_t vertex_count;        ///< Number of vertices.

  GMDL_Obj_TexCoord * texcoords; ///< Texture coordinates, or NULL.
  size_t texcoord_count;         ///< Number of texture coordinates.

  GMDL_Obj_Normal * normals; ///< Normals, or NULL.
  size_t normal_count;       ///< Number of normals.

  GMDL_Obj_Face * faces; ///< Faces, or NULL.
  size_t face_count;     ///< Number of faces.

  GMDL_Obj_Line * lines; ///< Polylines (`l`), or NULL.
  size_t line_count;     ///< Number of polylines.

  GMDL_Obj_Line_Vertex * line_vertices; ///< Every polyline's references.
  size_t line_vertex_count;             ///< Number of those references.

  /**
   * Point elements (`p`), as vertex indices, or NULL.
   *
   * One `p` statement declares one point per index it names, so unlike `l`
   * there is nothing to group: the statement boundary carries no meaning
   * that survives parsing.
   */
  int32_t * points;
  size_t point_count; ///< Number of points.

  GMDL_Obj_Group * groups; ///< Groups and objects, or NULL.
  size_t group_count;      ///< Number of groups.

  GMDL_Obj_Material_Mapping * material_mappings; ///< Mappings, or NULL.
  size_t material_mapping_count;                 ///< Number of mappings.

  char mtllib[GMDL_OBJ_MAX_PATH_LENGTH]; ///< `mtllib` path, or "" if absent.

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
