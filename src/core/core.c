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
 * Result strings and default limits.
 */

#include <ghoti.io/model/macros.h>
#include <ghoti.io/model/core.h>

const char * gmdl_result_string(GMDL_Result result) {
  switch (result) {
    case GMDL_OK:
      return "No error";
    case GMDL_ERR_IO:
      return "I/O error";
    case GMDL_ERR_FORMAT:
      return "Invalid file format";
    case GMDL_ERR_UNSUPPORTED:
      return "Unsupported feature";
    case GMDL_ERR_LIMIT:
      return "Limit exceeded";
    case GMDL_ERR_CORRUPT:
      return "Corrupt data";
    case GMDL_ERR_OOM:
      return "Out of memory";
    case GMDL_ERR_INVALID:
      return "Invalid argument";
    case GMDL_ERR_INTERNAL:
      return "Internal error";
    case GMDL_RESULT_COUNT:
    default:
      return "Unknown error";
  }
}

void gmdl_limits_default(GMDL_Limits * limits) {
  if (!limits) {
    return;
  }

  // A line cap is the one limit that has to have a value: the parser reads a
  // line at a time, so without it a single unterminated line would be read
  // into memory in its entirety. The record caps are left open because the
  // size of the input already bounds them - every record costs at least a
  // couple of bytes - and a legitimate model can be very large.
  *limits = (GMDL_Limits) {
    .max_line_length = 65536,
    .max_vertices = 0,
    .max_texcoords = 0,
    .max_normals = 0,
    .max_faces = 0,
    .max_face_indices = 0,
    .max_groups = 0,
    .max_materials = 0,
    .max_statements = 0,
    .max_mtllibs = 0,
    .max_maplibs = 0,
    .max_maps = 0,
    .max_render_states = 0,
    .max_shadow_objs = 0,
    .max_trace_objs = 0,
  };
}
