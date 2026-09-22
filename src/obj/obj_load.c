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
 * Wavefront OBJ parsing.
 *
 * Reference documents:
 *   https://en.wikipedia.org/wiki/Wavefront_.obj_file
 *   https://paulbourke.net/dataformats/obj/
 *   https://paulbourke.net/dataformats/obj/obj_spec.pdf
 */

#include <ghoti.io/cutil/allocator.h>
#include <ghoti.io/cutil/array.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <ghoti.io/model/macros.h>
#include <ghoti.io/model/obj.h>

#include "obj_internal.h"
#include "../core/number_internal.h"

/**
 * The arrays an OBJ file builds up while it is being parsed.
 *
 * The parser used to grow six arrays by hand, each with its own doubling step
 * and its own out-of-memory branch. They are GCU_Arrays now, and the finished
 * contents are handed to the model with gcu_array_steal(), so the public
 * GMDL_Obj still exposes plain pointers and counts.
 */
typedef struct {
  GCU_Array vertices;
  /**
   * Vertex colours, empty until the file's first coloured `v` line.
   *
   * The invariant between lines is that this holds either zero entries or
   * exactly as many as @c vertices, so obj_color_append() has to backfill
   * absent entries for whatever came before the first colour.
   */
  GCU_Array colors;
  GCU_Array texcoords;
  GCU_Array normals;
  GCU_Array faces;
  GCU_Array lines;
  GCU_Array line_vertices;
  GCU_Array points;
  GCU_Array groups;
  GCU_Array material_mappings;
  GCU_Array statements;
  const GMDL_Allocator * allocator;
} obj_builder_t;

static bool obj_builder_init(
    obj_builder_t * b, const GMDL_Allocator * allocator) {
  memset(b, 0, sizeof(*b));
  b->allocator = allocator;
  return gcu_array_create_in_place(
             &b->vertices, sizeof(GMDL_Obj_Vertex), 128, allocator)
      && gcu_array_create_in_place(
          &b->colors, sizeof(GMDL_Obj_Color), 128, allocator)
      && gcu_array_create_in_place(
          &b->texcoords, sizeof(GMDL_Obj_TexCoord), 128, allocator)
      && gcu_array_create_in_place(
          &b->normals, sizeof(GMDL_Obj_Normal), 128, allocator)
      && gcu_array_create_in_place(
          &b->faces, sizeof(GMDL_Obj_Face), 128, allocator)
      && gcu_array_create_in_place(
          &b->lines, sizeof(GMDL_Obj_Line), 16, allocator)
      && gcu_array_create_in_place(
          &b->line_vertices, sizeof(GMDL_Obj_Line_Vertex), 32, allocator)
      && gcu_array_create_in_place(
          &b->points, sizeof(GMDL_Obj_Point), 16, allocator)
      && gcu_array_create_in_place(
          &b->groups, sizeof(GMDL_Obj_Group), 16, allocator)
      && gcu_array_create_in_place(&b->material_mappings,
          sizeof(GMDL_Obj_Material_Mapping), 4, allocator)
      && gcu_array_create_in_place(
          &b->statements, sizeof(GMDL_Obj_Statement), 4, allocator);
}

/**
 * Release everything the builder holds, including the per-face overflow
 * arrays, which the model's own free function would otherwise be responsible
 * for once the faces reached it.
 */
static void obj_builder_destroy(obj_builder_t * b) {
  for (size_t i = 0; i < gcu_array_count(&b->faces); i++) {
    GMDL_Obj_Face * face = (GMDL_Obj_Face *)gcu_array_at(&b->faces, i);
    gcu_allocator_free(b->allocator, face->overflow);
  }
  for (size_t i = 0; i < gcu_array_count(&b->statements); i++) {
    GMDL_Obj_Statement * statement =
        (GMDL_Obj_Statement *)gcu_array_at(&b->statements, i);
    gcu_allocator_free(b->allocator, statement->text);
  }
  gcu_array_destroy_in_place(&b->vertices);
  gcu_array_destroy_in_place(&b->colors);
  gcu_array_destroy_in_place(&b->texcoords);
  gcu_array_destroy_in_place(&b->normals);
  gcu_array_destroy_in_place(&b->faces);
  gcu_array_destroy_in_place(&b->lines);
  gcu_array_destroy_in_place(&b->line_vertices);
  gcu_array_destroy_in_place(&b->points);
  gcu_array_destroy_in_place(&b->groups);
  gcu_array_destroy_in_place(&b->material_mappings);
  gcu_array_destroy_in_place(&b->statements);
}

/**
 * How many elements count against `max_faces`.
 *
 * Faces, polylines and points share one budget. They used to have one each -
 * three separate `gmdl_limit_reached()` calls against the same field - so a
 * caller who set `max_faces` to bound memory from untrusted input got up to
 * three times what they asked for, silently, on the one axis the field
 * exists for. Section 5's table has always described it as a single budget.
 *
 * A `p` line declares one element per index it names, not one per statement,
 * so `p 1 2 3` costs three. That is what the model stores.
 */
static size_t obj_element_count(const obj_builder_t * b) {
  return gcu_array_count(&b->faces) + gcu_array_count(&b->lines)
      + gcu_array_count(&b->points);
}

/** Move one array into a model's pointer and count. */
static void obj_steal_into(
    GCU_Array * array, void ** out_data, size_t * out_count) {
  // Trim first, so the model does not carry the parser's spare capacity.
  (void)gcu_array_shrink_to_fit(array);
  size_t count = 0;
  *out_data = gcu_array_steal(array, &count);
  *out_count = count;
}

/**
 * Read up to @p max numbers from @p rest, returning how many were there.
 *
 * `strtof` skips leading whitespace and reports where it stopped, so a token
 * is a number exactly when the conversion consumed anything at all. That is a
 * *prefix* rule, and it is deliberately not the whole-token rule
 * mtl_token_float() uses: there, "-o 1 2" is followed by a path and a
 * half-eaten "2.png" would become a texture called ".png", so a token has to
 * be all number or nothing. A `v` or `vt` line has no path at the end for a
 * partial token to corrupt, both reference importers read "0abc" as 0 there,
 * and section 3.1 has said "text after the numbers is ignored" since before
 * colours arrived. Two rules, two reasons; they are not a copy of each other
 * that drifted.
 *
 * @param rest The text after the directive.
 * @param out Receives the numbers. At least @p max entries.
 * @param max How many to read before stopping, however many follow.
 * @return How many numbers were read, 0 to @p max.
 */
static size_t obj_take_floats(const char * rest, float * out, size_t max) {
  size_t taken = 0;
  while (taken < max) {
    char * end = NULL;
    float value = strtof(rest, &end);
    if (end == rest) {
      break;
    }
    out[taken++] = value;
    rest = end;
  }
  return taken;
}

/** The colour an uncoloured vertex gets: white, which multiplies to nothing. */
static const GMDL_Obj_Color obj_color_absent = {1.0f, 1.0f, 1.0f, false};

/**
 * Record @p color for the vertex that was just appended.
 *
 * Restores the builder's invariant first: a file whose hundredth `v` line is
 * the first with a colour needs ninety-nine absent entries before it, because
 * the array is indexed by vertex number and nothing else records which vertex
 * a colour belongs to.
 *
 * @param b The builder, whose @c vertices already holds the new vertex.
 * @param color The colour, present or absent.
 * @return false only on allocation failure.
 */
static bool obj_color_append(obj_builder_t * b, const GMDL_Obj_Color * color) {
  size_t wanted = gcu_array_count(&b->vertices);
  while (gcu_array_count(&b->colors) + 1 < wanted) {
    if (!gcu_array_append(&b->colors, (void *)&obj_color_absent)) {
      return false;
    }
  }
  return gcu_array_append(&b->colors, (void *)color);
}

/**
 * Read one face vertex reference, refusing anything that is not one.
 *
 * A token is "v", "v/vt", "v//vn" or "v/vt/vn", with an optional fourth
 * '/'-separated field that is ignored (3.5). Every field that is present must
 * be an integer and nothing but a '/' may follow one, so "1.5" is the integer
 * 1 followed by junk rather than a reference to vertex 1 - which is what
 * section 3.5 calls GMDL_ERR_FORMAT. Accepting it instead turned a typo into
 * a face pointing at the wrong vertex, silently.
 *
 * The fields are read positionally so that an omitted one stays distinct from
 * a present one. Rewriting every '/' as a space and handing the result to
 * sscanf cannot do that: "1//2" and "1 2" become the same string, so the
 * normal was read as the texture coordinate and then discarded, and "1/2" was
 * treated as a missing texture coordinate for the same reason.
 *
 * @param token First byte of the token.
 * @param token_end One past its last byte - the whitespace or terminator
 *   that ended it. The token is not NUL-terminated on its own.
 * @param out_v Receives the vertex index as written, 0 when absent.
 * @param out_vt Receives the texture coordinate index, 0 when absent.
 * @param out_vn Receives the normal index, 0 when absent.
 * @return true when the token is a well-formed reference.
 */
static bool obj_parse_face_token(const char * token, const char * token_end,
    long * out_v, long * out_vt, long * out_vn) {
  *out_v = 0;
  *out_vt = 0;
  *out_vn = 0;

  const char * cursor = token;
  char * end = NULL;

  // The vertex index is the one field that must be present.
  long value = strtol(cursor, &end, 10);
  if (end == cursor) {
    return false;
  }
  *out_v = value;
  cursor = end;
  if (cursor == token_end) {
    return true;
  }
  if (*cursor != '/') {
    return false;
  }

  // "v//vn" leaves the texture coordinate empty. "1/" leaves it empty too,
  // and means the same thing as "1"; nothing in section 3.5 makes that an
  // error, and no writer emits it either way.
  cursor++;
  if (cursor < token_end && *cursor != '/') {
    value = strtol(cursor, &end, 10);
    if (end == cursor) {
      return false;
    }
    *out_vt = value;
    cursor = end;
  }
  if (cursor == token_end) {
    return true;
  }
  if (*cursor != '/') {
    return false;
  }

  cursor++;
  if (cursor < token_end && *cursor != '/') {
    value = strtol(cursor, &end, 10);
    if (end == cursor) {
      return false;
    }
    *out_vn = value;
    cursor = end;
  }
  if (cursor == token_end) {
    return true;
  }
  // A fourth '/'-separated field is ignored (3.5). Anything else is junk.
  return *cursor == '/';
}

/**
 * Convert an OBJ index to a 0-based one, or -1 when absent.
 *
 * OBJ indices are 1-based, and a negative index is relative: -1 names the most
 * recently declared element of that kind, -2 the one before it, and so on.
 * The specification measures that from the current position in the file, so
 * the count has to be the one at the moment the face is read rather than the
 * final total - which is why this is resolved here rather than left to the
 * caller.
 *
 * @param value The index as written, 0 when the field was absent.
 * @param declared How many elements of that kind have been read so far.
 * @return The 0-based index, -1 when absent, or an out-of-range value when the
 *   file names an element that does not exist. Callers are expected to range
 *   check against the final counts; a bogus index is not by itself a reason to
 *   reject the file, and readers differ on how to treat one.
 */
static int32_t obj_index(long value, size_t declared) {
  if (value == 0) {
    return -1;
  }

  long resolved;
  if (value < 0) {
    // Relative: -1 is the last one declared.
    resolved = (long)declared + value;
  }
  else {
    resolved = value - 1;
  }

  // An index too large to represent must be held OUT of range, never
  // narrowed into it. "f 4294967297" resolves to 4294967296, whose low 32
  // bits are zero: narrowing would point the face at vertex 0 - a vertex the
  // file never named, in range, and accepted by exactly the range check
  // section 1 makes the consumer responsible for. That division of labour
  // only works while an unrepresentable index cannot arrive disguised as a
  // valid one.
  //
  // INT32_MAX and INT32_MIN are safe to saturate to because this is an
  // int32_t: a model with INT32_MAX elements of a kind cannot have its later
  // ones addressed through this API whatever we return here, so no reachable
  // element loses its index to the clamp.
  if (resolved > INT32_MAX) {
    return INT32_MAX;
  }
  if (resolved < INT32_MIN) {
    return INT32_MIN;
  }
  return (int32_t)resolved;
}

/**
 * Record a `call` or `csh` statement without acting on it.
 *
 * The text is kept exactly as written, trailing blanks removed. Nothing is
 * split, resolved or executed: see ::GMDL_Obj_Statement for why.
 *
 * @param rest The text after the directive, already past leading blanks.
 * @param kind Which directive it was.
 * @param allocator The allocator for the copy.
 * @param statements The array to append to.
 * @return ::GMDL_OK, ::GMDL_ERR_FORMAT when there is no text, or
 *   ::GMDL_ERR_OOM.
 */
static GMDL_Result obj_record_statement(const char * rest,
    GMDL_Obj_Statement_Kind kind, const GMDL_Allocator * allocator,
    GCU_Array * statements) {
  size_t length = strlen(rest);
  while (length > 0 && (rest[length - 1] == ' ' || rest[length - 1] == '\t')) {
    length--;
  }
  if (length == 0) {
    return GMDL_ERR_FORMAT; // "call" or "csh" naming nothing.
  }

  char * copy = gcu_allocator_malloc(allocator, length + 1);
  if (!copy) {
    return GMDL_ERR_OOM;
  }
  memcpy(copy, rest, length);
  copy[length] = '\0';

  GMDL_Obj_Statement * stored =
      (GMDL_Obj_Statement *)gcu_array_emplace(statements);
  if (!stored) {
    gcu_allocator_free(allocator, copy);
    return GMDL_ERR_OOM;
  }
  stored->kind = kind;
  stored->text = copy;
  return GMDL_OK;
}

static GMDL_Result obj_load_pinned(GMDL_Stream * stream,
    const GMDL_Limits * limits, const GMDL_Allocator * allocator,
    GMDL_Obj ** out_obj) {
  if (!out_obj) {
    return GMDL_ERR_INVALID;
  }
  *out_obj = NULL;
  if (!stream) {
    return GMDL_ERR_INVALID;
  }

  GMDL_Limits defaults;
  if (!limits) {
    gmdl_limits_default(&defaults);
    limits = &defaults;
  }

  size_t line_size = limits->max_line_length ? limits->max_line_length : 65536;
  // Room for the NUL that gmdl_stream_read_line() always writes.
  char * line = gcu_allocator_malloc(allocator, line_size + 1);
  if (!line) {
    return GMDL_ERR_OOM;
  }

  obj_builder_t builder;
  if (!obj_builder_init(&builder, allocator)) {
    obj_builder_destroy(&builder);
    gcu_allocator_free(allocator, line);
    return GMDL_ERR_OOM;
  }

  GMDL_Result result = GMDL_OK;
  char mtllib[GMDL_OBJ_MAX_PATH_LENGTH];
  mtllib[0] = '\0';

  long current_group = -1;          // Index of the active group.
  int32_t current_material = -1;    // Material set by the last "usemtl".
  // Smoothing group set by the last "s"; 0 is the format's own default, so a
  // file with no "s" line leaves every face at 0 and writes none back.
  int32_t current_smoothing = 0;

  GMDL_Line_Reader reader;
  gmdl_line_reader_init(&reader, stream, line, line_size);

  for (;;) {
    const char * line_text = NULL;
    GMDL_Result line_result = gmdl_line_next(&reader, &line_text);
    if (line_result == GMDL_ERR_IO) {
      break; // End of stream.
    }
    if (line_result != GMDL_OK) {
      result = line_result; // A line too long to hold.
      goto cleanup;
    }

    const char * rest = NULL;
    if (gmdl_line_is(line_text, "v", &rest)) {
      // Six or more numbers means the `r g b` extension, and the colour is
      // fields four to six whether or not a `w` might have been intended to
      // sit among them - which is what Blender does, measured on four, five,
      // six and seven numbers. Four or five is a plain vertex with a `w` or
      // with junk after it, and neither carries a colour.
      float number[6];
      size_t count = obj_take_floats(rest, number, 6);
      if (count < 3) {
        result = GMDL_ERR_FORMAT;
        goto cleanup;
      }
      GMDL_Obj_Vertex v = {number[0], number[1], number[2]};
      GMDL_Obj_Color color = obj_color_absent;
      if (count >= 6) {
        color = (GMDL_Obj_Color){number[3], number[4], number[5], true};
      }
      if (gmdl_limit_reached(
              gcu_array_count(&builder.vertices), limits->max_vertices)) {
        result = GMDL_ERR_LIMIT;
        goto cleanup;
      }
      if (!gcu_array_append(&builder.vertices, &v)) {
        result = GMDL_ERR_OOM;
        goto cleanup;
      }
      if ((color.present || gcu_array_count(&builder.colors) != 0)
          && !obj_color_append(&builder, &color)) {
        result = GMDL_ERR_OOM;
        goto cleanup;
      }
    }
    else if (gmdl_line_is(line_text, "vt", &rest)) {
      // `vt u [v] [w]`: only u is required, and both of the others default to
      // zero. The reference importers disagree here - Blender reads "vt 0.5"
      // and VTK calls it an error - so this follows the specification and the
      // more permissive of the two. `w` is read and dropped; nothing in this
      // model is three-dimensional in texture space.
      float number[3];
      size_t count = obj_take_floats(rest, number, 3);
      if (count < 1) {
        result = GMDL_ERR_FORMAT;
        goto cleanup;
      }
      GMDL_Obj_TexCoord vt = {number[0], count >= 2 ? number[1] : 0.0f};
      if (gmdl_limit_reached(
              gcu_array_count(&builder.texcoords), limits->max_texcoords)) {
        result = GMDL_ERR_LIMIT;
        goto cleanup;
      }
      if (!gcu_array_append(&builder.texcoords, &vt)) {
        result = GMDL_ERR_OOM;
        goto cleanup;
      }
    }
    else if (gmdl_line_is(line_text, "vn", &rest)) {
      float number[3];
      if (obj_take_floats(rest, number, 3) < 3) {
        result = GMDL_ERR_FORMAT;
        goto cleanup;
      }
      GMDL_Obj_Normal vn = {number[0], number[1], number[2]};
      if (gmdl_limit_reached(
              gcu_array_count(&builder.normals), limits->max_normals)) {
        result = GMDL_ERR_LIMIT;
        goto cleanup;
      }
      if (!gcu_array_append(&builder.normals, &vn)) {
        result = GMDL_ERR_OOM;
        goto cleanup;
      }
    }
    else if (gmdl_line_is(line_text, "f", &rest)) {
      if (gmdl_limit_reached(
              obj_element_count(&builder), limits->max_faces)) {
        result = GMDL_ERR_LIMIT;
        goto cleanup;
      }

      // Counts at this point in the file, which is what a negative (relative)
      // index is measured against.
      size_t vertex_count = gcu_array_count(&builder.vertices);
      size_t texcoord_count = gcu_array_count(&builder.texcoords);
      size_t normal_count = gcu_array_count(&builder.normals);

      GMDL_Obj_Face face;
      memset(&face, 0, sizeof(face));
      face.count = 0;
      face.material_index = current_material;
      face.smoothing_group = current_smoothing;
      face.overflow = NULL;

      // Vertices past the fourth go into an overflow array, which is handed to
      // the face at the end of the line.
      GCU_Array overflow;
      if (!gcu_array_create_in_place(
              &overflow, sizeof(GMDL_Obj_Face_Overflow), 0, allocator)) {
        result = GMDL_ERR_OOM;
        goto cleanup;
      }

      const char * cursor = rest;
      while (*cursor) {
        while (*cursor == ' ' || *cursor == '\t') {
          cursor++;
        }
        if (!*cursor) {
          break;
        }
        const char * token = cursor;
        while (*cursor && *cursor != ' ' && *cursor != '\t') {
          cursor++;
        }

        long v = 0;
        long vt = 0;
        long vn = 0;
        if (!obj_parse_face_token(token, cursor, &v, &vt, &vn)) {
          gcu_array_destroy_in_place(&overflow);
          result = GMDL_ERR_FORMAT;
          goto cleanup;
        }

        if (gmdl_limit_reached(face.count, limits->max_face_indices)) {
          gcu_array_destroy_in_place(&overflow);
          result = GMDL_ERR_LIMIT;
          goto cleanup;
        }

        if (face.count < 4) {
          face.vertex[face.count] = obj_index(v, vertex_count);
          face.texcoord[face.count] = obj_index(vt, texcoord_count);
          face.normal[face.count] = obj_index(vn, normal_count);
          face.count++;
        }
        else {
          GMDL_Obj_Face_Overflow * extra =
              (GMDL_Obj_Face_Overflow *)gcu_array_emplace(&overflow);
          if (!extra) {
            gcu_array_destroy_in_place(&overflow);
            result = GMDL_ERR_OOM;
            goto cleanup;
          }
          extra->vertex = obj_index(v, vertex_count);
          extra->texcoord = obj_index(vt, texcoord_count);
          extra->normal = obj_index(vn, normal_count);
          face.count++;
        }
      }

      // Trim before handing it over: this block outlives the parse and there
      // may be one per face.
      (void)gcu_array_shrink_to_fit(&overflow);
      face.overflow = (GMDL_Obj_Face_Overflow *)gcu_array_steal(&overflow, NULL);
      gcu_array_destroy_in_place(&overflow);

      if (!gcu_array_append(&builder.faces, &face)) {
        // The face never reached the array, so its overflow will not be freed
        // along with the rest.
        gcu_allocator_free(allocator, face.overflow);
        result = GMDL_ERR_OOM;
        goto cleanup;
      }
      if (current_group >= 0) {
        GMDL_Obj_Group * group = (GMDL_Obj_Group *)gcu_array_at(
            &builder.groups, (size_t)current_group);
        group->face_count++;
      }
    }
    else if (gmdl_line_is(line_text, "g", &rest)
        || gmdl_line_is(line_text, "o", &rest)) {
      // Which of the two it was, so the dump can write back the spelling the
      // file used. They behave identically here and do not mean the same
      // thing to the tools that write them.
      bool is_object = line_text[0] == 'o';
      // "g" with no name means the default group, per the specification; a
      // name too long for the field is refused rather than cut, because the
      // first 127 bytes of a name name something else (3.9).
      //
      // The whole line is the name, as it is for `mtllib` and `usemtl`
      // (3.7, 3.8). The specification does describe `g a b` as two group
      // names, and neither reference implements that: Blender reads the line
      // as one group called "alpha beta". Taking the first token is wrong
      // under *both* readings - it renames the group under the reference's
      // and discards a name under the specification's - while the whole-line
      // reading keeps every byte the file wrote, so a model that one day
      // supports several names per line can still split it (12).
      char name[GMDL_OBJ_MAX_NAME_LENGTH];
      GMDL_Result named = gmdl_rest_of_line(rest, name, sizeof(name));
      if (named == GMDL_ERR_FORMAT) {
        memcpy(name, "default", sizeof("default"));
      }
      else if (named != GMDL_OK) {
        result = named;
        goto cleanup;
      }
      if (gmdl_limit_reached(
              gcu_array_count(&builder.groups), limits->max_groups)) {
        result = GMDL_ERR_LIMIT;
        goto cleanup;
      }
      GMDL_Obj_Group * group =
          (GMDL_Obj_Group *)gcu_array_emplace(&builder.groups);
      if (!group) {
        result = GMDL_ERR_OOM;
        goto cleanup;
      }
      memset(group, 0, sizeof(*group));
      memcpy(group->name, name, strlen(name) + 1);
      group->start_face = gcu_array_count(&builder.faces);
      group->face_count = 0;
      group->is_object = is_object;
      current_group = (long)gcu_array_count(&builder.groups) - 1;
    }
    else if (gmdl_line_is(line_text, "l", &rest)) {
      if (gmdl_limit_reached(
              obj_element_count(&builder), limits->max_faces)) {
        result = GMDL_ERR_LIMIT;
        goto cleanup;
      }

      size_t vertex_count = gcu_array_count(&builder.vertices);
      size_t texcoord_count = gcu_array_count(&builder.texcoords);

      GMDL_Obj_Line element;
      element.start = gcu_array_count(&builder.line_vertices);
      element.count = 0;
      element.material_index = current_material;

      const char * cursor = rest;
      while (*cursor) {
        while (*cursor == ' ' || *cursor == '\t') {
          cursor++;
        }
        if (!*cursor) {
          break;
        }
        const char * token = cursor;
        while (*cursor && *cursor != ' ' && *cursor != '\t') {
          cursor++;
        }

        long v = 0;
        long vt = 0;
        long vn = 0;
        // The same token grammar as a face, and for the same reason: a file
        // that writes "1//2" on an l line is using a spelling the format
        // does not give lines, and refusing it would reject a file every
        // other reader accepts. The normal is read and dropped - a polyline
        // has nothing to do with one.
        if (!obj_parse_face_token(token, cursor, &v, &vt, &vn)) {
          result = GMDL_ERR_FORMAT;
          goto cleanup;
        }
        if (gmdl_limit_reached(element.count, limits->max_face_indices)) {
          result = GMDL_ERR_LIMIT;
          goto cleanup;
        }

        GMDL_Obj_Line_Vertex * entry =
            (GMDL_Obj_Line_Vertex *)gcu_array_emplace(&builder.line_vertices);
        if (!entry) {
          result = GMDL_ERR_OOM;
          goto cleanup;
        }
        entry->vertex = obj_index(v, vertex_count);
        entry->texcoord = obj_index(vt, texcoord_count);
        element.count++;
      }

      if (element.count == 0) {
        result = GMDL_ERR_FORMAT; // "l" naming nothing is not a polyline.
        goto cleanup;
      }
      GMDL_Obj_Line * stored =
          (GMDL_Obj_Line *)gcu_array_emplace(&builder.lines);
      if (!stored) {
        result = GMDL_ERR_OOM;
        goto cleanup;
      }
      *stored = element;
    }
    else if (gmdl_line_is(line_text, "p", &rest)) {
      size_t vertex_count = gcu_array_count(&builder.vertices);
      size_t declared = 0;

      const char * cursor = rest;
      while (*cursor) {
        while (*cursor == ' ' || *cursor == '\t') {
          cursor++;
        }
        if (!*cursor) {
          break;
        }
        char * end = NULL;
        long v = strtol(cursor, &end, 10);
        if (end == cursor) {
          result = GMDL_ERR_FORMAT;
          goto cleanup;
        }
        // A point is a vertex index and nothing else, so unlike a face or a
        // line there is no '/' form to accept.
        if (*end && *end != ' ' && *end != '\t') {
          result = GMDL_ERR_FORMAT;
          goto cleanup;
        }
        cursor = end;

        if (gmdl_limit_reached(
                obj_element_count(&builder), limits->max_faces)) {
          result = GMDL_ERR_LIMIT;
          goto cleanup;
        }
        GMDL_Obj_Point * stored =
            (GMDL_Obj_Point *)gcu_array_emplace(&builder.points);
        if (!stored) {
          result = GMDL_ERR_OOM;
          goto cleanup;
        }
        stored->vertex = obj_index(v, vertex_count);
        stored->material_index = current_material;
        declared++;
      }

      if (declared == 0) {
        result = GMDL_ERR_FORMAT;
        goto cleanup;
      }
    }
    else if (gmdl_line_is(line_text, "s", &rest)) {
      // "s off" is the format's own spelling of zero, and "s 0" means the
      // same. Anything else must be a group number.
      if (gmdl_line_is(rest, "off", NULL)) {
        current_smoothing = 0;
      }
      else {
        int32_t value = 0;
        GMDL_Result parsed = gmdl_parse_int32(rest, &value);
        if (parsed != GMDL_OK) {
          result = parsed;
          goto cleanup;
        }
        current_smoothing = value;
      }
    }
    // "call" and "csh" are recorded and never acted on. A parser that ran a
    // command out of its own input would make every .obj a program; the
    // caller knows where the file came from and this does not.
    else if (gmdl_line_is(line_text, "call", &rest)) {
      if (gmdl_limit_reached(
              gcu_array_count(&builder.statements), limits->max_statements)) {
        result = GMDL_ERR_LIMIT;
        goto cleanup;
      }
      GMDL_Result recorded = obj_record_statement(
          rest, GMDL_OBJ_STATEMENT_CALL, allocator, &builder.statements);
      if (recorded != GMDL_OK) {
        result = recorded;
        goto cleanup;
      }
    }
    else if (gmdl_line_is(line_text, "csh", &rest)) {
      if (gmdl_limit_reached(
              gcu_array_count(&builder.statements), limits->max_statements)) {
        result = GMDL_ERR_LIMIT;
        goto cleanup;
      }
      GMDL_Result recorded = obj_record_statement(
          rest, GMDL_OBJ_STATEMENT_CSH, allocator, &builder.statements);
      if (recorded != GMDL_OK) {
        result = recorded;
        goto cleanup;
      }
    }
    else if (gmdl_line_is(line_text, "usemtl", &rest)) {
      // A bare "usemtl" is GMDL_ERR_FORMAT (3.7); an over-long one is
      // GMDL_ERR_LIMIT (3.9). gmdl_rest_of_line() distinguishes them, and
      // takes the whole line because a material name may contain spaces and
      // `newmtl` reads its own the same way - a name truncated on one side
      // and not the other would stop matching.
      char mtl_name[GMDL_OBJ_MAX_NAME_LENGTH];
      GMDL_Result named = gmdl_rest_of_line(rest, mtl_name, sizeof(mtl_name));
      if (named != GMDL_OK) {
        result = named;
        goto cleanup;
      }

      int32_t mapped = -1;
      for (size_t i = 0; i < gcu_array_count(&builder.material_mappings); i++) {
        GMDL_Obj_Material_Mapping * mapping =
            (GMDL_Obj_Material_Mapping *)gcu_array_at(
                &builder.material_mappings, i);
        if (strcmp(mapping->name, mtl_name) == 0) {
          mapped = mapping->index;
          break;
        }
      }
      if (mapped < 0) {
        if (gmdl_limit_reached(gcu_array_count(&builder.material_mappings),
                limits->max_materials)) {
          result = GMDL_ERR_LIMIT;
          goto cleanup;
        }
        GMDL_Obj_Material_Mapping * mapping =
            (GMDL_Obj_Material_Mapping *)gcu_array_emplace(
                &builder.material_mappings);
        if (!mapping) {
          result = GMDL_ERR_OOM;
          goto cleanup;
        }
        memset(mapping, 0, sizeof(*mapping));
        memcpy(mapping->name, mtl_name, strlen(mtl_name) + 1);
        mapping->index =
            (int32_t)gcu_array_count(&builder.material_mappings) - 1;
        mapped = mapping->index;
      }
      current_material = mapped;
    }
    else if (gmdl_line_is(line_text, "mtllib", &rest)) {
      // The whole line is the path, spaces included - Blender exports
      // `mtllib my model.mtl` for a document saved under that name, and
      // taking the first token off it names a file that does not exist.
      // A bare "mtllib" clears the path (3.8); one too long for the field is
      // GMDL_ERR_LIMIT (3.9), since a path cut at 255 bytes names a
      // different file, or none.
      GMDL_Result named = gmdl_rest_of_line(rest, mtllib, sizeof(mtllib));
      if (named == GMDL_ERR_FORMAT) {
        mtllib[0] = '\0';
      }
      else if (named != GMDL_OK) {
        result = named;
        goto cleanup;
      }
    }
    // Anything else - comments, unsupported directives - is ignored, which is
    // what the OBJ specification asks readers to do.
  }

  // Parsing succeeded: build the model and move the arrays into it. Doing this
  // last means there is no half-built model to unwind on the error path.
  {
    GMDL_Obj * obj = gcu_allocator_calloc(allocator, 1, sizeof(GMDL_Obj));
    if (!obj) {
      result = GMDL_ERR_OOM;
      goto cleanup;
    }

    obj_steal_into(
        &builder.vertices, (void **)&obj->vertices, &obj->vertex_count);
    obj_steal_into(&builder.colors, (void **)&obj->colors, &obj->color_count);
    obj_steal_into(
        &builder.texcoords, (void **)&obj->texcoords, &obj->texcoord_count);
    obj_steal_into(&builder.normals, (void **)&obj->normals,
        &obj->normal_count);
    obj_steal_into(&builder.faces, (void **)&obj->faces, &obj->face_count);
    obj_steal_into(&builder.lines, (void **)&obj->lines, &obj->line_count);
    obj_steal_into(&builder.line_vertices, (void **)&obj->line_vertices,
        &obj->line_vertex_count);
    obj_steal_into(&builder.points, (void **)&obj->points, &obj->point_count);
    obj_steal_into(&builder.groups, (void **)&obj->groups, &obj->group_count);
    obj_steal_into(&builder.material_mappings,
        (void **)&obj->material_mappings, &obj->material_mapping_count);
    obj_steal_into(&builder.statements, (void **)&obj->statements,
        &obj->statement_count);

    memcpy(obj->mtllib, mtllib, sizeof(obj->mtllib));
    obj->allocator = allocator;

    obj_builder_destroy(&builder);
    gcu_allocator_free(allocator, line);
    *out_obj = obj;
    return GMDL_OK;
  }

cleanup:
  obj_builder_destroy(&builder);
  gcu_allocator_free(allocator, line);
  return result;
}

/**
 * Read an OBJ document with the numeric locale pinned.
 *
 * The pin is here, around the whole parse, rather than at each of the
 * conversions inside it. See src/core/number_internal.h for why: the thing
 * worth making impossible is a future directive whose author does not know
 * this rule exists.
 */
GMDL_Result gmdl_obj_load(GMDL_Stream * stream, const GMDL_Limits * limits,
    const GMDL_Allocator * allocator, GMDL_Obj ** out_obj) {
  GMDL_Numeric_Scope numeric;
  gmdl_numeric_scope_begin(&numeric);
  GMDL_Result result = obj_load_pinned(stream, limits, allocator, out_obj);
  gmdl_numeric_scope_end(&numeric);
  return result;
}

GMDL_Result gmdl_obj_load_file(const char * path, const GMDL_Limits * limits,
    const GMDL_Allocator * allocator, GMDL_Obj ** out_obj) {
  if (!out_obj) {
    return GMDL_ERR_INVALID;
  }
  *out_obj = NULL;

  GMDL_Stream * stream = NULL;
  GMDL_Result result = gmdl_stream_create_file(path, allocator, &stream);
  if (result != GMDL_OK) {
    return result;
  }

  result = gmdl_obj_load(stream, limits, allocator, out_obj);
  gmdl_stream_destroy(stream);
  return result;
}

void gmdl_obj_free(GMDL_Obj * obj) {
  if (!obj) {
    return;
  }
  const GMDL_Allocator * allocator = obj->allocator;

  gcu_allocator_free(allocator, obj->vertices);
  gcu_allocator_free(allocator, obj->colors);
  gcu_allocator_free(allocator, obj->texcoords);
  gcu_allocator_free(allocator, obj->normals);
  if (obj->faces) {
    for (size_t i = 0; i < obj->face_count; i++) {
      gcu_allocator_free(allocator, obj->faces[i].overflow);
    }
    gcu_allocator_free(allocator, obj->faces);
  }
  gcu_allocator_free(allocator, obj->lines);
  gcu_allocator_free(allocator, obj->line_vertices);
  gcu_allocator_free(allocator, obj->points);
  gcu_allocator_free(allocator, obj->groups);
  gcu_allocator_free(allocator, obj->material_mappings);
  if (obj->statements) {
    for (size_t i = 0; i < obj->statement_count; i++) {
      gcu_allocator_free(allocator, obj->statements[i].text);
    }
    gcu_allocator_free(allocator, obj->statements);
  }
  gcu_allocator_free(allocator, obj);
}
