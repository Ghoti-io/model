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
 * Writing a parsed STL document back out.
 */

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <ghoti.io/model/macros.h>
#include <ghoti.io/model/stl.h>

#include "../core/number_internal.h"
#include "stl_internal.h"

static void stl_write_u16_le(uint8_t * p, uint16_t value) {
  p[0] = (uint8_t)(value & 0xffu);
  p[1] = (uint8_t)((value >> 8) & 0xffu);
}

static void stl_write_u32_le(uint8_t * p, uint32_t value) {
  p[0] = (uint8_t)(value & 0xffu);
  p[1] = (uint8_t)((value >> 8) & 0xffu);
  p[2] = (uint8_t)((value >> 16) & 0xffu);
  p[3] = (uint8_t)((value >> 24) & 0xffu);
}

static void stl_write_f32_le(uint8_t * p, float value) {
  uint32_t bits = 0;
  memcpy(&bits, &value, sizeof(bits));
  stl_write_u32_le(p, bits);
}

static void stl_fill_header_color(uint8_t header[GMDL_STL_HEADER_SIZE],
    const float color[4]) {
  header[0] = 'C';
  header[1] = 'O';
  header[2] = 'L';
  header[3] = 'O';
  header[4] = 'R';
  header[5] = '=';
  for (int i = 0; i < 4; i++) {
    float c = color[i];
    if (c < 0.0f) {
      c = 0.0f;
    }
    if (c > 1.0f) {
      c = 1.0f;
    }
    header[6 + i] = (uint8_t)(c * 255.0f + 0.5f);
  }
}

static GMDL_Result stl_dump_binary(const GMDL_Stl * stl,
    const GMDL_Stl_Options * options, FILE * fd) {
  uint8_t header[GMDL_STL_HEADER_SIZE];
  if (stl->form == GMDL_STL_FORM_BINARY) {
    memcpy(header, stl->header, sizeof(header));
  } else {
    memset(header, 0, sizeof(header));
  }
  if (options->color_convention == GMDL_STL_COLOR_MAGICS
      && (stl->present & GMDL_STL_HAS_DEFAULT_COLOR)) {
    stl_fill_header_color(header, stl->default_color);
  }
  if (fwrite(header, 1, sizeof(header), fd) != sizeof(header)) {
    return GMDL_ERR_IO;
  }

  uint8_t count_bytes[4];
  if (stl->triangle_count > UINT32_MAX) {
    return GMDL_ERR_LIMIT;
  }
  stl_write_u32_le(count_bytes, (uint32_t)stl->triangle_count);
  if (fwrite(count_bytes, 1, sizeof(count_bytes), fd) != sizeof(count_bytes)) {
    return GMDL_ERR_IO;
  }

  for (size_t i = 0; i < stl->triangle_count; i++) {
    const GMDL_Stl_Triangle * tri = &stl->triangles[i];
    uint8_t record[50];
    for (int k = 0; k < 3; k++) {
      stl_write_f32_le(record + (size_t)k * 4, tri->normal[k]);
    }
    for (int v = 0; v < 3; v++) {
      for (int k = 0; k < 3; k++) {
        stl_write_f32_le(
            record + 12 + (size_t)v * 12 + (size_t)k * 4, tri->vertex[v][k]);
      }
    }
    uint16_t attribute = tri->attribute;
    if ((tri->present & GMDL_STL_TRI_HAS_COLOR)
        && options->color_convention != GMDL_STL_COLOR_NONE) {
      attribute = stl_pack_color(
          options->color_convention, tri->r, tri->g, tri->b);
    }
    stl_write_u16_le(record + 48, attribute);
    if (fwrite(record, 1, sizeof(record), fd) != sizeof(record)) {
      return GMDL_ERR_IO;
    }
  }
  return GMDL_OK;
}

static GMDL_Result stl_dump_ascii(const GMDL_Stl * stl, FILE * fd) {
  const char * name = stl->solid_name[0] ? stl->solid_name : "";
  if (fprintf(fd, "solid %s\n", name) < 0) {
    return GMDL_ERR_IO;
  }
  for (size_t i = 0; i < stl->triangle_count; i++) {
    const GMDL_Stl_Triangle * tri = &stl->triangles[i];
    if (fprintf(fd, "  facet normal %.9g %.9g %.9g\n",
            (double)tri->normal[0], (double)tri->normal[1],
            (double)tri->normal[2])
        < 0) {
      return GMDL_ERR_IO;
    }
    if (fprintf(fd, "    outer loop\n") < 0) {
      return GMDL_ERR_IO;
    }
    for (int v = 0; v < 3; v++) {
      if (fprintf(fd, "      vertex %.9g %.9g %.9g\n",
              (double)tri->vertex[v][0], (double)tri->vertex[v][1],
              (double)tri->vertex[v][2])
          < 0) {
        return GMDL_ERR_IO;
      }
    }
    if (fprintf(fd, "    endloop\n  endfacet\n") < 0) {
      return GMDL_ERR_IO;
    }
  }
  if (fprintf(fd, "endsolid %s\n", name) < 0) {
    return GMDL_ERR_IO;
  }
  return GMDL_OK;
}

static GMDL_Result stl_dump_pinned(const GMDL_Stl * stl,
    const GMDL_Stl_Options * options, FILE * fd) {
  if (!stl || !fd) {
    return GMDL_ERR_INVALID;
  }
  GMDL_Stl_Options defaults;
  if (!options) {
    gmdl_stl_options_default(&defaults);
    options = &defaults;
  }
  if (options->write_ascii) {
    return stl_dump_ascii(stl, fd);
  }
  return stl_dump_binary(stl, options, fd);
}

GMDL_Result gmdl_stl_dump(const GMDL_Stl * stl,
    const GMDL_Stl_Options * options, FILE * fd) {
  GMDL_Numeric_Scope numeric;
  gmdl_numeric_scope_begin(&numeric);
  GMDL_Result result = stl_dump_pinned(stl, options, fd);
  gmdl_numeric_scope_end(&numeric);
  return result;
}
