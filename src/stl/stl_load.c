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
 * STL parsing: ASCII and binary, with optional VisCAM / Magics colour.
 */

#include <ctype.h>
#include <errno.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

#include <ghoti.io/cutil/allocator.h>
#include <ghoti.io/cutil/array.h>

#include <ghoti.io/model/macros.h>
#include <ghoti.io/model/stl.h>

#include "../core/number_internal.h"
#include "stl_internal.h"

static bool stl_limit_reached(size_t count, size_t limit) {
  return limit != 0 && count >= limit;
}

static uint16_t stl_read_u16_le(const uint8_t * p) {
  return (uint16_t)p[0] | ((uint16_t)p[1] << 8);
}

static uint32_t stl_read_u32_le(const uint8_t * p) {
  return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16)
      | ((uint32_t)p[3] << 24);
}

static float stl_read_f32_le(const uint8_t * p) {
  uint32_t bits = stl_read_u32_le(p);
  float value = 0.0f;
  memcpy(&value, &bits, sizeof(value));
  return value;
}

static void stl_apply_triangle_color(GMDL_Stl_Triangle * tri,
    GMDL_Stl_Color_Convention convention) {
  float r = 0.0f;
  float g = 0.0f;
  float b = 0.0f;
  if (stl_decode_color(convention, tri->attribute, &r, &g, &b)) {
    tri->r = r;
    tri->g = g;
    tri->b = b;
    tri->present |= GMDL_STL_TRI_HAS_COLOR;
  }
}

/** Scan an 80-byte header for Magics `COLOR=` and four RGBA bytes. */
static void stl_parse_header_color(GMDL_Stl * stl) {
  for (size_t i = 0; i + 10 <= GMDL_STL_HEADER_SIZE; i++) {
    if (stl->header[i] == 'C' && stl->header[i + 1] == 'O'
        && stl->header[i + 2] == 'L' && stl->header[i + 3] == 'O'
        && stl->header[i + 4] == 'R' && stl->header[i + 5] == '=') {
      stl->default_color[0] = stl->header[i + 6] / 255.0f;
      stl->default_color[1] = stl->header[i + 7] / 255.0f;
      stl->default_color[2] = stl->header[i + 8] / 255.0f;
      stl->default_color[3] = stl->header[i + 9] / 255.0f;
      stl->present |= GMDL_STL_HAS_DEFAULT_COLOR;
      return;
    }
  }
}

static bool stl_keyword(const char * text, const char * keyword,
    const char ** out_rest, bool require_lowercase) {
  size_t length = strlen(keyword);
  for (size_t i = 0; i < length; i++) {
    unsigned char a = (unsigned char)text[i];
    unsigned char b = (unsigned char)keyword[i];
    if (require_lowercase) {
      if (a != b) {
        return false;
      }
    } else if (tolower(a) != tolower(b)) {
      return false;
    }
  }
  char next = text[length];
  if (next != '\0' && next != ' ' && next != '\t') {
    return false;
  }
  if (out_rest) {
    const char * rest = text + length;
    while (*rest == ' ' || *rest == '\t') {
      rest++;
    }
    *out_rest = rest;
  }
  return true;
}

/** When the stored normal is all zeros, fill the unit normal of the triangle. */
static void stl_maybe_recompute_normal(GMDL_Stl_Triangle * tri, bool recompute) {
  if (!recompute) {
    return;
  }
  if (tri->normal[0] != 0.0f || tri->normal[1] != 0.0f
      || tri->normal[2] != 0.0f) {
    return;
  }
  float ax = tri->vertex[1][0] - tri->vertex[0][0];
  float ay = tri->vertex[1][1] - tri->vertex[0][1];
  float az = tri->vertex[1][2] - tri->vertex[0][2];
  float bx = tri->vertex[2][0] - tri->vertex[0][0];
  float by = tri->vertex[2][1] - tri->vertex[0][1];
  float bz = tri->vertex[2][2] - tri->vertex[0][2];
  float cx = ay * bz - az * by;
  float cy = az * bx - ax * bz;
  float cz = ax * by - ay * bx;
  float length = sqrtf(cx * cx + cy * cy + cz * cz);
  if (length == 0.0f) {
    return;
  }
  tri->normal[0] = cx / length;
  tri->normal[1] = cy / length;
  tri->normal[2] = cz / length;
}

static bool stl_floats_ok(const float * values, size_t count,
    bool reject_non_finite) {
  if (!reject_non_finite) {
    return true;
  }
  for (size_t i = 0; i < count; i++) {
    if (!isfinite(values[i])) {
      return false;
    }
  }
  return true;
}

static GMDL_Result stl_read_exact(GMDL_Stream * stream, void * buffer,
    size_t size) {
  size_t got = 0;
  GMDL_Result result = gmdl_stream_read(stream, buffer, size, &got);
  if (result != GMDL_OK) {
    return result;
  }
  if (got != size) {
    return GMDL_ERR_FORMAT;
  }
  return GMDL_OK;
}

static bool stl_size_is_binary(size_t size) {
  return size >= 84 && ((size - 84) % 50) == 0;
}

static bool stl_looks_like_ascii(GMDL_Stream * stream) {
  uint8_t peek[6];
  size_t got = 0;
  if (gmdl_stream_read(stream, peek, sizeof(peek), &got) != GMDL_OK
      || got < 5) {
    (void)gmdl_stream_seek(stream, 0);
    return false;
  }
  (void)gmdl_stream_seek(stream, 0);
  // "solid" with optional leading BOM is the ASCII start. A binary file may
  // also begin with those letters; size detection is preferred when it fits.
  size_t i = 0;
  if (got >= 3 && peek[0] == 0xEF && peek[1] == 0xBB && peek[2] == 0xBF) {
    i = 3;
  }
  if (got - i < 5) {
    return false;
  }
  return tolower(peek[i]) == 's' && tolower(peek[i + 1]) == 'o'
      && tolower(peek[i + 2]) == 'l' && tolower(peek[i + 3]) == 'i'
      && tolower(peek[i + 4]) == 'd'
      && (got - i == 5 || peek[i + 5] == ' ' || peek[i + 5] == '\t'
          || peek[i + 5] == '\r' || peek[i + 5] == '\n' || peek[i + 5] == '\0');
}

static GMDL_Result stl_load_binary(GMDL_Stream * stream,
    const GMDL_Stl_Options * limits, const GMDL_Allocator * allocator,
    GMDL_Stl ** out_stl) {
  GMDL_Result result = GMDL_OK;
  GCU_Array triangles;
  if (!gcu_array_create_in_place(
          &triangles, sizeof(GMDL_Stl_Triangle), 64, allocator)) {
    return GMDL_ERR_OOM;
  }

  uint8_t header[GMDL_STL_HEADER_SIZE];
  result = stl_read_exact(stream, header, sizeof(header));
  if (result != GMDL_OK) {
    goto cleanup;
  }
  uint8_t count_bytes[4];
  result = stl_read_exact(stream, count_bytes, sizeof(count_bytes));
  if (result != GMDL_OK) {
    goto cleanup;
  }
  uint32_t count = stl_read_u32_le(count_bytes);
  if (limits->max_triangles != 0 && count > limits->max_triangles) {
    result = GMDL_ERR_LIMIT;
    goto cleanup;
  }

  for (uint32_t i = 0; i < count; i++) {
    uint8_t record[50];
    result = stl_read_exact(stream, record, sizeof(record));
    if (result != GMDL_OK) {
      goto cleanup;
    }
    GMDL_Stl_Triangle tri;
    memset(&tri, 0, sizeof(tri));
    for (int k = 0; k < 3; k++) {
      tri.normal[k] = stl_read_f32_le(record + (size_t)k * 4);
    }
    for (int v = 0; v < 3; v++) {
      for (int k = 0; k < 3; k++) {
        tri.vertex[v][k] =
            stl_read_f32_le(record + 12 + (size_t)v * 12 + (size_t)k * 4);
      }
    }
    tri.attribute = stl_read_u16_le(record + 48);
    if (!stl_floats_ok(tri.normal, 3, limits->reject_non_finite)
        || !stl_floats_ok(tri.vertex[0], 3, limits->reject_non_finite)
        || !stl_floats_ok(tri.vertex[1], 3, limits->reject_non_finite)
        || !stl_floats_ok(tri.vertex[2], 3, limits->reject_non_finite)) {
      result = GMDL_ERR_FORMAT;
      goto cleanup;
    }
    stl_apply_triangle_color(&tri, limits->color_convention);
    stl_maybe_recompute_normal(&tri, limits->recompute_zero_normals);
    if (!gcu_array_append(&triangles, &tri)) {
      result = GMDL_ERR_OOM;
      goto cleanup;
    }
  }

  GMDL_Stl * stl = gcu_allocator_calloc(allocator, 1, sizeof(GMDL_Stl));
  if (!stl) {
    result = GMDL_ERR_OOM;
    goto cleanup;
  }
  stl->form = GMDL_STL_FORM_BINARY;
  memcpy(stl->header, header, sizeof(header));
  stl_parse_header_color(stl);
  (void)gcu_array_shrink_to_fit(&triangles);
  size_t n = 0;
  stl->triangles = (GMDL_Stl_Triangle *)gcu_array_steal(&triangles, &n);
  stl->triangle_count = n;
  stl->allocator = allocator;
  gcu_array_destroy_in_place(&triangles);
  *out_stl = stl;
  return GMDL_OK;

cleanup:
  gcu_array_destroy_in_place(&triangles);
  return result;
}

static GMDL_Result stl_take_floats(const char * text, float * out, size_t want,
    const char ** out_end) {
  const char * cursor = text;
  for (size_t i = 0; i < want; i++) {
    while (*cursor == ' ' || *cursor == '\t') {
      cursor++;
    }
    char * end = NULL;
    errno = 0;
    float value = strtof(cursor, &end);
    if (end == cursor || errno == ERANGE) {
      return GMDL_ERR_FORMAT;
    }
    out[i] = value;
    cursor = end;
  }
  if (out_end) {
    *out_end = cursor;
  }
  return GMDL_OK;
}

static GMDL_Result stl_load_ascii(GMDL_Stream * stream,
    const GMDL_Stl_Options * limits, const GMDL_Allocator * allocator,
    GMDL_Stl ** out_stl) {
  size_t line_size = limits->max_line_length ? limits->max_line_length
                                             : GMDL_DEFAULT_MAX_LINE_LENGTH;
  char * line = gcu_allocator_malloc(allocator, line_size + 1);
  if (!line) {
    return GMDL_ERR_OOM;
  }

  GMDL_Result result = GMDL_OK;
  GCU_Array triangles;
  if (!gcu_array_create_in_place(
          &triangles, sizeof(GMDL_Stl_Triangle), 64, allocator)) {
    gcu_allocator_free(allocator, line);
    return GMDL_ERR_OOM;
  }

  char solid_name[GMDL_STL_MAX_NAME_LENGTH];
  solid_name[0] = '\0';
  bool have_solid = false;
  bool in_facet = false;
  bool in_loop = false;
  GMDL_Stl_Triangle current;
  memset(&current, 0, sizeof(current));
  size_t vertices_in_facet = 0;

  bool lowercase = limits->require_lowercase_keywords;

  for (;;) {
    size_t length = 0;
    GMDL_Result line_result =
        gmdl_stream_read_line(stream, line, line_size + 1, &length);
    if (line_result == GMDL_ERR_IO) {
      break;
    }
    if (line_result != GMDL_OK) {
      result = line_result;
      goto cleanup;
    }

    const char * text = line;
    while (*text == ' ' || *text == '\t') {
      text++;
    }
    if (*text == '\0' || *text == '#') {
      continue;
    }

    const char * rest = NULL;
    if (!have_solid) {
      if (!stl_keyword(text, "solid", &rest, lowercase)) {
        result = GMDL_ERR_FORMAT;
        goto cleanup;
      }
      size_t name_len = strlen(rest);
      if (name_len >= sizeof(solid_name)) {
        result = GMDL_ERR_LIMIT;
        goto cleanup;
      }
      memcpy(solid_name, rest, name_len + 1);
      have_solid = true;
      continue;
    }

    if (stl_keyword(text, "endsolid", &rest, lowercase)) {
      if (in_facet) {
        result = GMDL_ERR_FORMAT;
        goto cleanup;
      }
      have_solid = false;
      break;
    }

    if (!in_facet) {
      if (!stl_keyword(text, "facet", &rest, lowercase)) {
        result = GMDL_ERR_FORMAT;
        goto cleanup;
      }
      const char * after_normal = NULL;
      if (!stl_keyword(rest, "normal", &after_normal, lowercase)) {
        result = GMDL_ERR_FORMAT;
        goto cleanup;
      }
      memset(&current, 0, sizeof(current));
      result = stl_take_floats(after_normal, current.normal, 3, NULL);
      if (result != GMDL_OK) {
        goto cleanup;
      }
      if (!stl_floats_ok(current.normal, 3, limits->reject_non_finite)) {
        result = GMDL_ERR_FORMAT;
        goto cleanup;
      }
      in_facet = true;
      in_loop = false;
      vertices_in_facet = 0;
      continue;
    }

    // Inside a facet: outer loop, vertices, endloop, then endfacet.
    if (!in_loop) {
      if (stl_keyword(text, "outer", &rest, lowercase)
          && stl_keyword(rest, "loop", NULL, lowercase)) {
        in_loop = true;
        continue;
      }
      if (stl_keyword(text, "endfacet", NULL, lowercase)) {
        if (vertices_in_facet != 3) {
          result = GMDL_ERR_FORMAT;
          goto cleanup;
        }
        stl_maybe_recompute_normal(&current, limits->recompute_zero_normals);
        if (stl_limit_reached(
                gcu_array_count(&triangles), limits->max_triangles)) {
          result = GMDL_ERR_LIMIT;
          goto cleanup;
        }
        if (!gcu_array_append(&triangles, &current)) {
          result = GMDL_ERR_OOM;
          goto cleanup;
        }
        in_facet = false;
        continue;
      }
      result = GMDL_ERR_FORMAT;
      goto cleanup;
    }

    if (stl_keyword(text, "vertex", &rest, lowercase)) {
      if (vertices_in_facet >= 3) {
        result = GMDL_ERR_FORMAT;
        goto cleanup;
      }
      result = stl_take_floats(
          rest, current.vertex[vertices_in_facet], 3, NULL);
      if (result != GMDL_OK) {
        goto cleanup;
      }
      if (!stl_floats_ok(
              current.vertex[vertices_in_facet], 3, limits->reject_non_finite)) {
        result = GMDL_ERR_FORMAT;
        goto cleanup;
      }
      vertices_in_facet++;
      continue;
    }

    if (stl_keyword(text, "endloop", NULL, lowercase)) {
      if (vertices_in_facet != 3) {
        result = GMDL_ERR_FORMAT;
        goto cleanup;
      }
      in_loop = false;
      continue;
    }

    result = GMDL_ERR_FORMAT;
    goto cleanup;
  }

  if (have_solid || in_facet) {
    result = GMDL_ERR_FORMAT;
    goto cleanup;
  }

  GMDL_Stl * stl = gcu_allocator_calloc(allocator, 1, sizeof(GMDL_Stl));
  if (!stl) {
    result = GMDL_ERR_OOM;
    goto cleanup;
  }
  stl->form = GMDL_STL_FORM_ASCII;
  memcpy(stl->solid_name, solid_name, sizeof(solid_name));
  (void)gcu_array_shrink_to_fit(&triangles);
  size_t n = 0;
  stl->triangles = (GMDL_Stl_Triangle *)gcu_array_steal(&triangles, &n);
  stl->triangle_count = n;
  stl->allocator = allocator;
  gcu_array_destroy_in_place(&triangles);
  gcu_allocator_free(allocator, line);
  *out_stl = stl;
  return GMDL_OK;

cleanup:
  gcu_array_destroy_in_place(&triangles);
  gcu_allocator_free(allocator, line);
  return result;
}

static GMDL_Result stl_load_pinned(GMDL_Stream * stream,
    const GMDL_Stl_Options * limits, const GMDL_Allocator * allocator,
    GMDL_Stl ** out_stl) {
  if (!stream || !out_stl) {
    return GMDL_ERR_INVALID;
  }
  *out_stl = NULL;

  GMDL_Stl_Options defaults;
  if (!limits) {
    gmdl_stl_options_default(&defaults);
    limits = &defaults;
  }
  if (!allocator) {
    allocator = gmdl_allocator_default();
  }

  bool as_binary = false;
  if (limits->force_binary) {
    as_binary = true;
  } else if (limits->force_ascii) {
    as_binary = false;
  } else if (stl_size_is_binary(gmdl_stream_size(stream))) {
    as_binary = true;
  } else if (stl_looks_like_ascii(stream)) {
    as_binary = false;
  } else {
    as_binary = true;
  }

  if (as_binary) {
    return stl_load_binary(stream, limits, allocator, out_stl);
  }
  return stl_load_ascii(stream, limits, allocator, out_stl);
}

void gmdl_stl_options_default(GMDL_Stl_Options * options) {
  if (!options) {
    return;
  }
  *options = (GMDL_Stl_Options) {
    .max_line_length = GMDL_DEFAULT_MAX_LINE_LENGTH,
  };
}

void gmdl_stl_options_blender(GMDL_Stl_Options * options) {
  if (!options) {
    return;
  }
  gmdl_stl_options_default(options);
  options->recompute_zero_normals = true;
  options->require_lowercase_keywords = true;
}

void gmdl_stl_options_freecad(GMDL_Stl_Options * options) {
  if (!options) {
    return;
  }
  gmdl_stl_options_default(options);
  options->recompute_zero_normals = true;
}

void gmdl_stl_options_openscad(GMDL_Stl_Options * options) {
  if (!options) {
    return;
  }
  gmdl_stl_options_default(options);
  options->recompute_zero_normals = true;
  options->require_lowercase_keywords = true;
}

GMDL_Result gmdl_stl_load(GMDL_Stream * stream, const GMDL_Stl_Options * limits,
    const GMDL_Allocator * allocator, GMDL_Stl ** out_stl) {
  GMDL_Numeric_Scope numeric;
  gmdl_numeric_scope_begin(&numeric);
  GMDL_Result result = stl_load_pinned(stream, limits, allocator, out_stl);
  gmdl_numeric_scope_end(&numeric);
  return result;
}

GMDL_Result gmdl_stl_load_file(const char * path,
    const GMDL_Stl_Options * limits, const GMDL_Allocator * allocator,
    GMDL_Stl ** out_stl) {
  if (!out_stl) {
    return GMDL_ERR_INVALID;
  }
  *out_stl = NULL;

  GMDL_Stream * stream = NULL;
  GMDL_Result result = gmdl_stream_create_file(path, allocator, &stream);
  if (result != GMDL_OK) {
    return result;
  }
  result = gmdl_stl_load(stream, limits, allocator, out_stl);
  gmdl_stream_destroy(stream);
  return result;
}

void gmdl_stl_free(GMDL_Stl * stl) {
  if (!stl) {
    return;
  }
  const GMDL_Allocator * allocator = stl->allocator;
  gcu_allocator_free(allocator, stl->triangles);
  gcu_allocator_free(allocator, stl);
}
