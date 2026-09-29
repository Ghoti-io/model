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
 * Writing a parsed OFF document back out.
 */

#include <stdbool.h>
#include <stdio.h>

#include <ghoti.io/model/macros.h>
#include <ghoti.io/model/off.h>

#include "../core/number_internal.h"

static const char * off_keyword(uint32_t present) {
  bool color = (present & GMDL_OFF_HAS_VERTEX_COLORS) != 0;
  bool normal = (present & GMDL_OFF_HAS_VERTEX_NORMALS) != 0;
  if (color && normal) {
    return "CNOFF";
  }
  if (color) {
    return "COFF";
  }
  if (normal) {
    return "NOFF";
  }
  return "OFF";
}

static GMDL_Result off_dump_pinned(const GMDL_Off * off,
    const GMDL_Off_Options * options, FILE * fd) {
  (void)options;
  if (!off || !fd) {
    return GMDL_ERR_INVALID;
  }

  if (fprintf(fd, "%s\n", off_keyword(off->present)) < 0) {
    return GMDL_ERR_IO;
  }
  if (fprintf(fd, "%zu %zu %zu\n", off->vertex_count, off->face_count,
          off->edge_count)
      < 0) {
    return GMDL_ERR_IO;
  }

  for (size_t i = 0; i < off->vertex_count; i++) {
    const GMDL_Off_Vertex * v = &off->vertices[i];
    if (fprintf(fd, "%.9g %.9g %.9g", (double)v->position[0],
            (double)v->position[1], (double)v->position[2])
        < 0) {
      return GMDL_ERR_IO;
    }
    if (v->present & GMDL_OFF_VERTEX_HAS_NORMAL) {
      if (fprintf(fd, " %.9g %.9g %.9g", (double)v->normal[0],
              (double)v->normal[1], (double)v->normal[2])
          < 0) {
        return GMDL_ERR_IO;
      }
    }
    if (v->present & GMDL_OFF_VERTEX_HAS_COLOR) {
      if (fprintf(fd, " %.9g %.9g %.9g %.9g", (double)v->r, (double)v->g,
              (double)v->b, (double)v->a)
          < 0) {
        return GMDL_ERR_IO;
      }
    }
    if (fprintf(fd, "\n") < 0) {
      return GMDL_ERR_IO;
    }
  }

  for (size_t i = 0; i < off->face_count; i++) {
    const GMDL_Off_Face * face = &off->faces[i];
    if (fprintf(fd, "%zu", face->index_count) < 0) {
      return GMDL_ERR_IO;
    }
    for (size_t c = 0; c < face->index_count; c++) {
      if (fprintf(fd, " %u", face->indices[c]) < 0) {
        return GMDL_ERR_IO;
      }
    }
    if (face->present & GMDL_OFF_FACE_HAS_COLOR_INDEX) {
      if (fprintf(fd, " %d", face->color_index) < 0) {
        return GMDL_ERR_IO;
      }
    } else if (face->present & GMDL_OFF_FACE_HAS_COLOR) {
      if (fprintf(fd, " %.9g %.9g %.9g %.9g", (double)face->r, (double)face->g,
              (double)face->b, (double)face->a)
          < 0) {
        return GMDL_ERR_IO;
      }
    }
    if (fprintf(fd, "\n") < 0) {
      return GMDL_ERR_IO;
    }
  }
  return GMDL_OK;
}

GMDL_Result gmdl_off_dump(const GMDL_Off * off,
    const GMDL_Off_Options * options, FILE * fd) {
  GMDL_Off_Options defaults;
  if (!options) {
    gmdl_off_options_default(&defaults);
    options = &defaults;
  }
  GMDL_Numeric_Scope numeric;
  gmdl_numeric_scope_begin(&numeric);
  GMDL_Result result = off_dump_pinned(off, options, fd);
  gmdl_numeric_scope_end(&numeric);
  return result;
}
