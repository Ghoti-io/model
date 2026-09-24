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
 * Core types, result codes, and limits for the Ghoti.io Model library.
 */

#ifndef GHOTI_IO_GMDL_CORE_H
#define GHOTI_IO_GMDL_CORE_H

#include <ghoti.io/model/allocator.h>
#include <ghoti.io/model/macros.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Result code for model library operations.
 */
typedef enum {
  GMDL_OK = 0,          ///< Operation succeeded.
  GMDL_ERR_IO,          ///< I/O error (read/write/seek failed).
  GMDL_ERR_FORMAT,      ///< Unrecognized or invalid format.
  GMDL_ERR_UNSUPPORTED, ///< Feature or format not supported.
  GMDL_ERR_LIMIT,       ///< Resource or size limit exceeded.
  GMDL_ERR_CORRUPT,     ///< Corrupt or invalid data.
  GMDL_ERR_OOM,         ///< Out of memory.
  GMDL_ERR_INVALID,     ///< Invalid argument.
  GMDL_ERR_INTERNAL,    ///< Internal library error.
  GMDL_RESULT_COUNT
} GMDL_Result;

/**
 * @brief Convert a result code to a human-readable string.
 *
 * The returned string is statically allocated and must not be freed.
 *
 * @param result The result code.
 * @return A description of the result code, never NULL.
 */
GMDL_API const char * gmdl_result_string(GMDL_Result result);

/**
 * @brief Caps applied while parsing, so that a hostile or corrupt file cannot
 * make the library allocate without bound.
 *
 * Zero means "no limit" for every field. Pass NULL to a load function to use
 * gmdl_limits_default().
 */
typedef struct GMDL_Limits {
  size_t max_line_length; ///< Longest accepted line, in bytes.
  size_t max_vertices;    ///< Cap on `v` records.
  size_t max_texcoords;   ///< Cap on `vt` records.
  size_t max_normals;     ///< Cap on `vn` records.
  size_t max_param_vertices; ///< Cap on `vp` records.
  size_t max_faces;       ///< Cap on `f` records.
  size_t max_face_indices; ///< Cap on vertices in a single face.
  size_t max_groups;      ///< Cap on `g`/`o` records.
  size_t max_materials;   ///< Cap on `newmtl` records, and on OBJ material
                          ///< mappings.
  size_t max_statements;  ///< Cap on `call` and `csh` records.
  size_t max_mtllibs;     ///< Cap on `mtllib` records.
  size_t max_maplibs;     ///< Cap on `maplib` records.
  size_t max_maps;        ///< Cap on `usemap` name-to-index mappings.
  size_t max_render_states; ///< Cap on distinct render-attribute states.
  size_t max_shadow_objs; ///< Cap on `shadow_obj` records.
  size_t max_trace_objs;  ///< Cap on `trace_obj` records.
  size_t max_freeforms;   ///< Cap on `curv`, `curv2` and `surf` elements.
  size_t max_basis_values; ///< Cap on `bmat` values, across every line.
  size_t max_freeform_bodies; ///< Cap on `parm`, `trim`, `hole`, `scrv` and
                              ///< `sp` lines, across every element.
  size_t max_parm_values;  ///< Cap on `parm` values, across every line.
  size_t max_curve_refs;   ///< Cap on the curve references `trim`, `hole`
                           ///< and `scrv` name, across every line.
  size_t max_special_points; ///< Cap on `sp` indices, across every line.
  size_t max_connections;  ///< Cap on `con` records.
} GMDL_Limits;

/**
 * @brief Fill in the default limits.
 *
 * @param limits Structure to populate. NULL is ignored.
 */
GMDL_API void gmdl_limits_default(GMDL_Limits * limits);

#ifdef __cplusplus
}
#endif

#endif // GHOTI_IO_GMDL_CORE_H
