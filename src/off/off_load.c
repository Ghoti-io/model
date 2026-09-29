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
 * OFF parsing: ASCII Geomview OFF / COFF / NOFF / CNOFF.
 */

#include <ctype.h>
#include <errno.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

#include <ghoti.io/cutil/allocator.h>
#include <ghoti.io/cutil/array.h>

#include <ghoti.io/model/macros.h>
#include <ghoti.io/model/off.h>

#include "../core/number_internal.h"

typedef struct {
  bool has_color;
  bool has_normal;
  bool has_texcoord;
  bool dim4;
  bool ndim;
  bool binary;
} Off_Keyword;

static bool off_floats_ok(const float * values, size_t count,
    bool reject_non_finite) {
  if (!reject_non_finite) {
    return true;
  }
  for (size_t i = 0; i < count; i++) {
    if (!isfinite((double)values[i])) {
      return false;
    }
  }
  return true;
}

/**
 * Parse a Geomview keyword of the form [ST][C][N][4][n]OFF[BINARY].
 *
 * Returns false when the token is not in the OFF family.
 */
static bool off_parse_keyword(const char * token, Off_Keyword * out) {
  memset(out, 0, sizeof(*out));
  const char * p = token;
  if (p[0] == 'S' && p[1] == 'T') {
    out->has_texcoord = true;
    p += 2;
  }
  if (*p == 'C') {
    out->has_color = true;
    p++;
  }
  if (*p == 'N') {
    out->has_normal = true;
    p++;
  }
  if (*p == '4') {
    out->dim4 = true;
    p++;
  }
  if (*p == 'n') {
    out->ndim = true;
    p++;
  }
  if (!(p[0] == 'O' && p[1] == 'F' && p[2] == 'F')) {
    return false;
  }
  p += 3;
  if (*p == '\0') {
    return true;
  }
  if (strcmp(p, "BINARY") == 0) {
    out->binary = true;
    return true;
  }
  return false;
}

static bool off_keyword_from_line(const char * text, Off_Keyword * out,
    const char ** after_keyword) {
  while (*text == ' ' || *text == '\t') {
    text++;
  }
  if (*text == '\0' || *text == '#') {
    return false;
  }
  char word[32];
  size_t n = 0;
  while (text[n] && text[n] != ' ' && text[n] != '\t' && text[n] != '#'
      && n + 1 < sizeof(word)) {
    word[n] = text[n];
    n++;
  }
  if (n + 1 >= sizeof(word)
      && text[n] && text[n] != ' ' && text[n] != '\t' && text[n] != '#') {
    return false; // Keyword longer than the word buffer.
  }
  word[n] = '\0';
  if (!off_parse_keyword(word, out)) {
    return false;
  }
  const char * rest = text + n;
  while (*rest == ' ' || *rest == '\t') {
    rest++;
  }
  if (strncmp(rest, "BINARY", 6) == 0
      && (rest[6] == '\0' || rest[6] == ' ' || rest[6] == '\t'
          || rest[6] == '#')) {
    out->binary = true;
    rest += 6;
    while (*rest == ' ' || *rest == '\t') {
      rest++;
    }
  }
  if (after_keyword) {
    *after_keyword = rest;
  }
  return true;
}

static GMDL_Result off_take_float(const char ** cursor, float * out) {
  while (**cursor == ' ' || **cursor == '\t') {
    (*cursor)++;
  }
  if (**cursor == '\0' || **cursor == '#') {
    return GMDL_ERR_FORMAT;
  }
  char * end = NULL;
  errno = 0;
  float value = strtof(*cursor, &end);
  if (end == *cursor || errno == ERANGE) {
    return GMDL_ERR_FORMAT;
  }
  *out = value;
  *cursor = end;
  return GMDL_OK;
}

static GMDL_Result off_take_u32(const char ** cursor, uint32_t * out) {
  while (**cursor == ' ' || **cursor == '\t') {
    (*cursor)++;
  }
  if (**cursor == '\0' || **cursor == '#') {
    return GMDL_ERR_FORMAT;
  }
  // Reject a leading sign; counts and indices are unsigned decimal.
  if (**cursor == '+' || **cursor == '-') {
    return GMDL_ERR_FORMAT;
  }
  char * end = NULL;
  errno = 0;
  unsigned long value = strtoul(*cursor, &end, 10);
  if (end == *cursor || errno == ERANGE || value > UINT32_MAX) {
    return GMDL_ERR_FORMAT;
  }
  *out = (uint32_t)value;
  *cursor = end;
  return GMDL_OK;
}

static bool off_line_has_more(const char * cursor) {
  while (*cursor == ' ' || *cursor == '\t') {
    cursor++;
  }
  return *cursor != '\0' && *cursor != '#';
}

static bool off_token_looks_integer(const char * start, const char * end) {
  for (const char * p = start; p < end; p++) {
    if (*p == '.' || *p == 'e' || *p == 'E') {
      return false;
    }
  }
  return true;
}

/**
 * Consume a colourspec: empty, one integer index, or RGB[A] as ints or floats.
 */
static GMDL_Result off_take_colorspec(const char * text, float * r, float * g,
    float * b, float * a, int32_t * color_index, uint32_t * present_bits) {
  *present_bits = 0;
  *a = 1.0f;
  *color_index = 0;
  if (!off_line_has_more(text)) {
    return GMDL_OK;
  }

  float values[4];
  bool as_int[4];
  size_t count = 0;
  const char * cursor = text;
  while (count < 4 && off_line_has_more(cursor)) {
    const char * start = cursor;
    while (*start == ' ' || *start == '\t') {
      start++;
    }
    GMDL_Result result = off_take_float(&cursor, &values[count]);
    if (result != GMDL_OK) {
      return result;
    }
    as_int[count] = off_token_looks_integer(start, cursor);
    count++;
  }
  if (off_line_has_more(cursor) || count == 2) {
    return GMDL_ERR_FORMAT;
  }
  if (count == 1) {
    // Colormap indices are signed 32-bit. Casting a float outside that range
    // is undefined; refuse before the cast.
    if (!as_int[0] || !isfinite((double)values[0])
        || values[0] < (float)INT32_MIN || values[0] > (float)INT32_MAX) {
      return GMDL_ERR_FORMAT;
    }
    int32_t index = (int32_t)values[0];
    if ((float)index != values[0]) {
      return GMDL_ERR_FORMAT;
    }
    *color_index = index;
    *present_bits = GMDL_OFF_FACE_HAS_COLOR_INDEX;
    return GMDL_OK;
  }
  bool all_int = as_int[0] && as_int[1] && as_int[2]
      && (count == 3 || as_int[3]);
  bool look_byte = all_int
      && (values[0] > 1.0f || values[1] > 1.0f || values[2] > 1.0f
          || (count == 4 && values[3] > 1.0f));
  if (look_byte) {
    *r = values[0] / 255.0f;
    *g = values[1] / 255.0f;
    *b = values[2] / 255.0f;
    *a = count == 4 ? values[3] / 255.0f : 1.0f;
  } else {
    *r = values[0];
    *g = values[1];
    *b = values[2];
    *a = count == 4 ? values[3] : 1.0f;
  }
  *present_bits = GMDL_OFF_FACE_HAS_COLOR;
  return GMDL_OK;
}

static GMDL_Result off_read_nonempty_line(GMDL_Stream * stream, char * line,
    size_t line_cap) {
  for (;;) {
    size_t length = 0;
    GMDL_Result result =
        gmdl_stream_read_line(stream, line, line_cap, &length);
    if (result != GMDL_OK) {
      return result;
    }
    const char * text = line;
    while (*text == ' ' || *text == '\t') {
      text++;
    }
    if (*text == '\0' || *text == '#') {
      continue;
    }
    return GMDL_OK;
  }
}

static void off_faces_cleanup(GCU_Array * faces,
    const GMDL_Allocator * allocator) {
  if (!faces) {
    return;
  }
  size_t n = gcu_array_count(faces);
  for (size_t i = 0; i < n; i++) {
    GMDL_Off_Face * face = (GMDL_Off_Face *)gcu_array_at(faces, i);
    gcu_allocator_free(allocator, face->indices);
    face->indices = NULL;
  }
}

static GMDL_Result off_load_pinned(GMDL_Stream * stream,
    const GMDL_Off_Options * limits, const GMDL_Allocator * allocator,
    GMDL_Off ** out_off) {
  if (!stream || !out_off) {
    return GMDL_ERR_INVALID;
  }
  *out_off = NULL;

  GMDL_Off_Options defaults;
  if (!limits) {
    gmdl_off_options_default(&defaults);
    limits = &defaults;
  }
  if (!allocator) {
    allocator = gmdl_allocator_default();
  }

  size_t line_size = limits->max_line_length ? limits->max_line_length
                                            : GMDL_DEFAULT_MAX_LINE_LENGTH;
  char * line = gcu_allocator_malloc(allocator, line_size + 1);
  if (!line) {
    return GMDL_ERR_OOM;
  }

  GCU_Array vertices;
  GCU_Array faces;
  memset(&vertices, 0, sizeof(vertices));
  memset(&faces, 0, sizeof(faces));
  bool arrays_live = false;

  GMDL_Result result =
      off_read_nonempty_line(stream, line, line_size + 1);
  if (result == GMDL_ERR_IO) {
    result = GMDL_ERR_FORMAT;
    goto fail;
  }
  if (result != GMDL_OK) {
    goto fail;
  }

  Off_Keyword keyword;
  const char * after = NULL;
  if (!off_keyword_from_line(line, &keyword, &after)) {
    result = GMDL_ERR_FORMAT;
    goto fail;
  }
  if (keyword.binary || keyword.dim4 || keyword.ndim || keyword.has_texcoord) {
    result = GMDL_ERR_UNSUPPORTED;
    goto fail;
  }

  uint32_t nvertices = 0;
  uint32_t nfaces = 0;
  uint32_t nedges = 0;
  if (off_line_has_more(after)) {
    result = off_take_u32(&after, &nvertices);
    if (result != GMDL_OK) {
      goto fail;
    }
    result = off_take_u32(&after, &nfaces);
    if (result != GMDL_OK) {
      goto fail;
    }
    result = off_take_u32(&after, &nedges);
    if (result != GMDL_OK) {
      goto fail;
    }
    if (off_line_has_more(after)) {
      result = GMDL_ERR_FORMAT;
      goto fail;
    }
  } else {
    result = off_read_nonempty_line(stream, line, line_size + 1);
    if (result == GMDL_ERR_IO) {
      result = GMDL_ERR_FORMAT;
      goto fail;
    }
    if (result != GMDL_OK) {
      goto fail;
    }
    const char * cursor = line;
    while (*cursor == ' ' || *cursor == '\t') {
      cursor++;
    }
    result = off_take_u32(&cursor, &nvertices);
    if (result != GMDL_OK) {
      goto fail;
    }
    result = off_take_u32(&cursor, &nfaces);
    if (result != GMDL_OK) {
      goto fail;
    }
    result = off_take_u32(&cursor, &nedges);
    if (result != GMDL_OK) {
      goto fail;
    }
    if (off_line_has_more(cursor)) {
      result = GMDL_ERR_FORMAT;
      goto fail;
    }
  }

  // Header states the final counts; refuse before allocating for them.
  if (limits->max_vertices != 0 && nvertices > limits->max_vertices) {
    result = GMDL_ERR_LIMIT;
    goto fail;
  }
  if (limits->max_faces != 0 && nfaces > limits->max_faces) {
    result = GMDL_ERR_LIMIT;
    goto fail;
  }

  if (!gcu_array_create_in_place(
          &vertices, sizeof(GMDL_Off_Vertex), 64, allocator)
      || !gcu_array_create_in_place(
          &faces, sizeof(GMDL_Off_Face), 64, allocator)) {
    result = GMDL_ERR_OOM;
    gcu_array_destroy_in_place(&vertices);
    gcu_array_destroy_in_place(&faces);
    goto fail;
  }
  arrays_live = true;

  for (uint32_t i = 0; i < nvertices; i++) {
    result = off_read_nonempty_line(stream, line, line_size + 1);
    if (result == GMDL_ERR_IO) {
      result = GMDL_ERR_FORMAT;
      goto fail;
    }
    if (result != GMDL_OK) {
      goto fail;
    }
    const char * cursor = line;
    while (*cursor == ' ' || *cursor == '\t') {
      cursor++;
    }
    GMDL_Off_Vertex vertex;
    memset(&vertex, 0, sizeof(vertex));
    vertex.a = 1.0f;
    result = off_take_float(&cursor, &vertex.position[0]);
    if (result != GMDL_OK) {
      goto fail;
    }
    result = off_take_float(&cursor, &vertex.position[1]);
    if (result != GMDL_OK) {
      goto fail;
    }
    result = off_take_float(&cursor, &vertex.position[2]);
    if (result != GMDL_OK) {
      goto fail;
    }
    if (!off_floats_ok(vertex.position, 3, limits->reject_non_finite)) {
      result = GMDL_ERR_FORMAT;
      goto fail;
    }
    if (keyword.has_normal) {
      result = off_take_float(&cursor, &vertex.normal[0]);
      if (result != GMDL_OK) {
        goto fail;
      }
      result = off_take_float(&cursor, &vertex.normal[1]);
      if (result != GMDL_OK) {
        goto fail;
      }
      result = off_take_float(&cursor, &vertex.normal[2]);
      if (result != GMDL_OK) {
        goto fail;
      }
      if (!off_floats_ok(vertex.normal, 3, limits->reject_non_finite)) {
        result = GMDL_ERR_FORMAT;
        goto fail;
      }
      vertex.present |= GMDL_OFF_VERTEX_HAS_NORMAL;
    }
    if (keyword.has_color) {
      float rgba[4] = {0.0f, 0.0f, 0.0f, 1.0f};
      int32_t unused_index = 0;
      uint32_t bits = 0;
      result = off_take_colorspec(
          cursor, &rgba[0], &rgba[1], &rgba[2], &rgba[3], &unused_index, &bits);
      if (result != GMDL_OK) {
        goto fail;
      }
      if (!(bits & GMDL_OFF_FACE_HAS_COLOR)) {
        // Vertex colour is RGB[A], never a colormap index, and is required
        // when the keyword carried C.
        result = GMDL_ERR_FORMAT;
        goto fail;
      }
      if (!off_floats_ok(rgba, 4, limits->reject_non_finite)) {
        result = GMDL_ERR_FORMAT;
        goto fail;
      }
      vertex.r = rgba[0];
      vertex.g = rgba[1];
      vertex.b = rgba[2];
      vertex.a = rgba[3];
      vertex.present |= GMDL_OFF_VERTEX_HAS_COLOR;
    } else if (off_line_has_more(cursor)) {
      result = GMDL_ERR_FORMAT;
      goto fail;
    }
    if (!gcu_array_append(&vertices, &vertex)) {
      result = GMDL_ERR_OOM;
      goto fail;
    }
  }

  for (uint32_t i = 0; i < nfaces; i++) {
    result = off_read_nonempty_line(stream, line, line_size + 1);
    if (result == GMDL_ERR_IO) {
      result = GMDL_ERR_FORMAT;
      goto fail;
    }
    if (result != GMDL_OK) {
      goto fail;
    }
    const char * cursor = line;
    while (*cursor == ' ' || *cursor == '\t') {
      cursor++;
    }
    uint32_t corners = 0;
    result = off_take_u32(&cursor, &corners);
    if (result != GMDL_OK) {
      goto fail;
    }
    if (corners < 3) {
      result = GMDL_ERR_FORMAT;
      goto fail;
    }
    if (limits->max_face_corners != 0 && corners > limits->max_face_corners) {
      result = GMDL_ERR_LIMIT;
      goto fail;
    }

    // Grow by append rather than allocating `corners` up front: a forged Nv
    // of billions would otherwise ask malloc for gigabytes before the line
    // proved it had only a handful of indices.
    GCU_Array index_buf;
    if (!gcu_array_create_in_place(
            &index_buf, sizeof(uint32_t), 8, allocator)) {
      result = GMDL_ERR_OOM;
      goto fail;
    }
    for (uint32_t c = 0; c < corners; c++) {
      uint32_t index = 0;
      result = off_take_u32(&cursor, &index);
      if (result != GMDL_OK) {
        gcu_array_destroy_in_place(&index_buf);
        goto fail;
      }
      if (index >= nvertices) {
        gcu_array_destroy_in_place(&index_buf);
        result = GMDL_ERR_FORMAT;
        goto fail;
      }
      if (!gcu_array_append(&index_buf, &index)) {
        gcu_array_destroy_in_place(&index_buf);
        result = GMDL_ERR_OOM;
        goto fail;
      }
    }
    (void)gcu_array_shrink_to_fit(&index_buf);
    size_t got = 0;
    uint32_t * indices = (uint32_t *)gcu_array_steal(&index_buf, &got);
    gcu_array_destroy_in_place(&index_buf);
    if (got != corners) {
      gcu_allocator_free(allocator, indices);
      result = GMDL_ERR_FORMAT;
      goto fail;
    }

    GMDL_Off_Face face;
    memset(&face, 0, sizeof(face));
    face.a = 1.0f;
    face.indices = indices;
    face.index_count = corners;
    result = off_take_colorspec(cursor, &face.r, &face.g, &face.b, &face.a,
        &face.color_index, &face.present);
    if (result != GMDL_OK) {
      gcu_allocator_free(allocator, indices);
      goto fail;
    }
    if (face.present & GMDL_OFF_FACE_HAS_COLOR) {
      float rgba[4] = {face.r, face.g, face.b, face.a};
      if (!off_floats_ok(rgba, 4, limits->reject_non_finite)) {
        gcu_allocator_free(allocator, indices);
        result = GMDL_ERR_FORMAT;
        goto fail;
      }
    }
    if (!gcu_array_append(&faces, &face)) {
      gcu_allocator_free(allocator, indices);
      result = GMDL_ERR_OOM;
      goto fail;
    }
  }

  GMDL_Off * off = gcu_allocator_calloc(allocator, 1, sizeof(GMDL_Off));
  if (!off) {
    result = GMDL_ERR_OOM;
    goto fail;
  }
  off->allocator = allocator;
  off->edge_count = nedges;
  if (keyword.has_normal) {
    off->present |= GMDL_OFF_HAS_VERTEX_NORMALS;
  }
  if (keyword.has_color) {
    off->present |= GMDL_OFF_HAS_VERTEX_COLORS;
  }

  (void)gcu_array_shrink_to_fit(&vertices);
  (void)gcu_array_shrink_to_fit(&faces);

  size_t vcount = 0;
  off->vertices = (GMDL_Off_Vertex *)gcu_array_steal(&vertices, &vcount);
  off->vertex_count = vcount;

  size_t fcount = 0;
  off->faces = (GMDL_Off_Face *)gcu_array_steal(&faces, &fcount);
  off->face_count = fcount;

  gcu_array_destroy_in_place(&vertices);
  gcu_array_destroy_in_place(&faces);
  gcu_allocator_free(allocator, line);
  *out_off = off;
  return GMDL_OK;

fail:
  if (arrays_live) {
    off_faces_cleanup(&faces, allocator);
    gcu_array_destroy_in_place(&vertices);
    gcu_array_destroy_in_place(&faces);
  }
  gcu_allocator_free(allocator, line);
  return result;
}

void gmdl_off_options_default(GMDL_Off_Options * options) {
  if (!options) {
    return;
  }
  *options = (GMDL_Off_Options) {
    .max_line_length = GMDL_DEFAULT_MAX_LINE_LENGTH,
  };
}

GMDL_Result gmdl_off_load(GMDL_Stream * stream, const GMDL_Off_Options * limits,
    const GMDL_Allocator * allocator, GMDL_Off ** out_off) {
  GMDL_Numeric_Scope numeric;
  gmdl_numeric_scope_begin(&numeric);
  GMDL_Result result = off_load_pinned(stream, limits, allocator, out_off);
  gmdl_numeric_scope_end(&numeric);
  return result;
}

GMDL_Result gmdl_off_load_file(const char * path,
    const GMDL_Off_Options * limits, const GMDL_Allocator * allocator,
    GMDL_Off ** out_off) {
  if (!out_off) {
    return GMDL_ERR_INVALID;
  }
  *out_off = NULL;
  GMDL_Stream * stream = NULL;
  GMDL_Result result = gmdl_stream_create_file(path, allocator, &stream);
  if (result != GMDL_OK) {
    return result;
  }
  result = gmdl_off_load(stream, limits, allocator, out_off);
  gmdl_stream_destroy(stream);
  return result;
}

void gmdl_off_free(GMDL_Off * off) {
  if (!off) {
    return;
  }
  const GMDL_Allocator * allocator = off->allocator;
  if (off->faces) {
    for (size_t i = 0; i < off->face_count; i++) {
      gcu_allocator_free(allocator, off->faces[i].indices);
    }
  }
  gcu_allocator_free(allocator, off->faces);
  gcu_allocator_free(allocator, off->vertices);
  gcu_allocator_free(allocator, off);
}
