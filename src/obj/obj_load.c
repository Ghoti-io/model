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
#include <errno.h>
#include <limits.h>
#include <math.h>
#include <stdint.h>
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
  /**
   * Homogeneous weights, empty until the file's first weighted `v` line.
   *
   * The same invariant as @c colors: zero entries, or exactly as many as
   * @c vertices.
   */
  GCU_Array weights;
  GCU_Array texcoords;
  GCU_Array normals;
  GCU_Array param_vertices;
  GCU_Array freeforms;
  GCU_Array freeform_vertices;
  GCU_Array basis_values;
  GCU_Array freeform_bodies;
  GCU_Array parm_values;
  GCU_Array curve_refs;
  GCU_Array special_points;
  GCU_Array connections;
  GCU_Array faces;
  GCU_Array lines;
  GCU_Array line_vertices;
  GCU_Array points;
  GCU_Array groups;
  GCU_Array material_mappings;
  GCU_Array map_mappings;
  GCU_Array render_states;
  GCU_Array statements;
  GCU_Array mtllibs;
  GCU_Array maplibs;
  GCU_Array shadow_objs;
  GCU_Array trace_objs;
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
          &b->weights, sizeof(GMDL_Obj_Weight), 128, allocator)
      && gcu_array_create_in_place(
          &b->texcoords, sizeof(GMDL_Obj_TexCoord), 128, allocator)
      && gcu_array_create_in_place(
          &b->normals, sizeof(GMDL_Obj_Normal), 128, allocator)
      && gcu_array_create_in_place(
          &b->param_vertices, sizeof(GMDL_Obj_Param_Vertex), 16, allocator)
      && gcu_array_create_in_place(
          &b->freeforms, sizeof(GMDL_Obj_Freeform), 4, allocator)
      && gcu_array_create_in_place(&b->freeform_vertices,
          sizeof(GMDL_Obj_Freeform_Vertex), 16, allocator)
      && gcu_array_create_in_place(
          &b->basis_values, sizeof(float), 16, allocator)
      && gcu_array_create_in_place(&b->freeform_bodies,
          sizeof(GMDL_Obj_Freeform_Body), 4, allocator)
      && gcu_array_create_in_place(
          &b->parm_values, sizeof(float), 16, allocator)
      && gcu_array_create_in_place(
          &b->curve_refs, sizeof(GMDL_Obj_Curve_Ref), 4, allocator)
      && gcu_array_create_in_place(
          &b->special_points, sizeof(int32_t), 4, allocator)
      && gcu_array_create_in_place(
          &b->connections, sizeof(GMDL_Obj_Connection), 4, allocator)
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
          &b->statements, sizeof(GMDL_Obj_Statement), 4, allocator)
      && gcu_array_create_in_place(&b->map_mappings,
          sizeof(GMDL_Obj_Map_Mapping), 4, allocator)
      && gcu_array_create_in_place(&b->render_states,
          sizeof(GMDL_Obj_Render_State), 4, allocator)
      && gcu_array_create_in_place(
          &b->mtllibs, sizeof(GMDL_Obj_Mtllib), 4, allocator)
      && gcu_array_create_in_place(
          &b->maplibs, sizeof(GMDL_Obj_Maplib), 4, allocator)
      && gcu_array_create_in_place(
          &b->shadow_objs, sizeof(GMDL_Obj_Render_Object), 4, allocator)
      && gcu_array_create_in_place(
          &b->trace_objs, sizeof(GMDL_Obj_Render_Object), 4, allocator);
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
  gcu_array_destroy_in_place(&b->mtllibs);
  gcu_array_destroy_in_place(&b->maplibs);
  gcu_array_destroy_in_place(&b->shadow_objs);
  gcu_array_destroy_in_place(&b->trace_objs);
  gcu_array_destroy_in_place(&b->vertices);
  gcu_array_destroy_in_place(&b->colors);
  gcu_array_destroy_in_place(&b->weights);
  gcu_array_destroy_in_place(&b->texcoords);
  gcu_array_destroy_in_place(&b->normals);
  gcu_array_destroy_in_place(&b->param_vertices);
  gcu_array_destroy_in_place(&b->freeforms);
  gcu_array_destroy_in_place(&b->freeform_vertices);
  gcu_array_destroy_in_place(&b->basis_values);
  gcu_array_destroy_in_place(&b->freeform_bodies);
  gcu_array_destroy_in_place(&b->parm_values);
  gcu_array_destroy_in_place(&b->curve_refs);
  gcu_array_destroy_in_place(&b->special_points);
  gcu_array_destroy_in_place(&b->connections);
  gcu_array_destroy_in_place(&b->faces);
  gcu_array_destroy_in_place(&b->lines);
  gcu_array_destroy_in_place(&b->line_vertices);
  gcu_array_destroy_in_place(&b->points);
  gcu_array_destroy_in_place(&b->groups);
  gcu_array_destroy_in_place(&b->material_mappings);
  gcu_array_destroy_in_place(&b->map_mappings);
  gcu_array_destroy_in_place(&b->render_states);
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

/**
 * Move one array out, returning its buffer and writing its length.
 *
 * It returns the buffer rather than taking a `void **` to write through,
 * because the caller's destination is a typed pointer: `(void **)&obj->faces`
 * would store a `void *` through an lvalue whose declared type is
 * `GMDL_Obj_Face *`, which is a strict-aliasing violation (C17 6.5p7).
 * Returning it makes the conversion an ordinary assignment from `void *`,
 * which is what that rule permits. gcc's `-Wall` level of
 * `-Wstrict-aliasing` did not report the old spelling; levels 1 and 2 did,
 * eleven times.
 */
static void * obj_steal_into(GCU_Array * array, size_t * out_count) {
  // Trim first, so the model does not carry the parser's spare capacity.
  (void)gcu_array_shrink_to_fit(array);
  size_t count = 0;
  void * data = gcu_array_steal(array, &count);
  *out_count = count;
  return data;
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
/** True when @p reject is set and one of @p values is nan or inf. */
static bool obj_rejected_non_finite(
    const float * values, size_t count, bool reject) {
  if (!reject) {
    return false;
  }
  for (size_t i = 0; i < count; i++) {
    if (!isfinite(values[i])) {
      return true;
    }
  }
  return false;
}

/** Replace nan and inf with zero. No effect unless @p zero_them is set. */
static void obj_zero_non_finite(float * values, size_t count, bool zero_them) {
  if (!zero_them) {
    return;
  }
  for (size_t i = 0; i < count; i++) {
    if (!isfinite(values[i])) {
      values[i] = 0.0f;
    }
  }
}

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

/** The weight an unweighted vertex gets: 1, which leaves the point as it is. */
static const GMDL_Obj_Weight obj_weight_absent = {1.0f, false};

/**
 * Record @p weight for the vertex that was just appended.
 *
 * The same backfill as obj_color_append(): the array is indexed by vertex
 * number, so the first weighted line has to fill in 1 for everything before
 * it.
 *
 * @param b The builder, whose @c vertices already holds the new vertex.
 * @param weight The weight, present or absent.
 * @return false only on allocation failure.
 */
static bool obj_weight_append(
    obj_builder_t * b, const GMDL_Obj_Weight * weight) {
  size_t wanted = gcu_array_count(&b->vertices);
  while (gcu_array_count(&b->weights) + 1 < wanted) {
    if (!gcu_array_append(&b->weights, (void *)&obj_weight_absent)) {
      return false;
    }
  }
  return gcu_array_append(&b->weights, (void *)weight);
}

/**
 * Append one group or object name.
 *
 * @param b The builder.
 * @param name The name, already known to fit the field.
 * @param is_object True for `o`, false for `g`.
 * @param joined True when this is not the first name on its line.
 * @param max_groups The cap, or 0 for none.
 * @return ::GMDL_OK, ::GMDL_ERR_LIMIT or ::GMDL_ERR_OOM.
 */
static GMDL_Result obj_append_group(obj_builder_t * b, const char * name,
    bool is_object, bool joined, size_t max_groups) {
  if (gmdl_limit_reached(gcu_array_count(&b->groups), max_groups)) {
    return GMDL_ERR_LIMIT;
  }
  GMDL_Obj_Group * group = (GMDL_Obj_Group *)gcu_array_emplace(&b->groups);
  if (!group) {
    return GMDL_ERR_OOM;
  }
  memset(group, 0, sizeof(*group));
  memcpy(group->name, name, strlen(name) + 1);
  group->start_face = gcu_array_count(&b->faces);
  group->is_object = is_object;
  group->joined = joined;
  return GMDL_OK;
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
// Indices are read as long long rather than long because long is 32 bits on
// Windows: strtol() saturated "f 2147483648" to LONG_MAX, which is INT32_MAX
// already, so the index arrived one lower than written, and every larger one
// arrived as that same in-range-looking value. long long is 64 bits on every
// platform this library builds for, as long already was on Linux.
static bool obj_parse_face_token(const char * token, const char * token_end,
    long long * out_v, long long * out_vt, long long * out_vn,
    bool reject_extra) {
  *out_v = 0;
  *out_vt = 0;
  *out_vn = 0;

  const char * cursor = token;
  char * end = NULL;

  // The vertex index is the one field that must be present.
  long long value = strtoll(cursor, &end, 10);
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
    value = strtoll(cursor, &end, 10);
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
    value = strtoll(cursor, &end, 10);
    if (end == cursor) {
      return false;
    }
    *out_vn = value;
    cursor = end;
  }
  if (cursor == token_end) {
    return true;
  }
  // A fourth '/'-separated field is ignored, unless the caller asked for it
  // to be a format error (3.5). Anything else is junk either way.
  if (*cursor == '/') {
    return !reject_extra;
  }
  return false;
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
static int32_t obj_index(long long value, size_t declared) {
  if (value == 0) {
    return -1;
  }

  long long resolved;
  if (value < 0) {
    // Relative: -1 is the last one declared.
    resolved = (long long)declared + value;
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
 * Copy the text after a directive, exactly as written, trailing blanks
 * removed.
 *
 * Used by the `call` and `csh` statements this parser refuses to execute
 * (3.13), which are the only lines it still keeps whole rather than reading.
 * `ctech`, `stech` and `mg` shared it until 3.18's model existed for them to
 * attach to. It stays a function of its own because the trimming and the
 * empty check are a rule about a directive's text rather than about those
 * two statements.
 *
 * @param rest The text after the directive, already past leading blanks.
 * @param allocator The allocator for the copy.
 * @param out Receives the copy, owned by the caller.
 * @return ::GMDL_OK, ::GMDL_ERR_FORMAT when there is no text, or
 *   ::GMDL_ERR_OOM.
 */
static GMDL_Result obj_copy_line_text(
    const char * rest, const GMDL_Allocator * allocator, char ** out) {
  size_t length = strlen(rest);
  while (length > 0 && (rest[length - 1] == ' ' || rest[length - 1] == '\t')) {
    length--;
  }
  if (length == 0) {
    return GMDL_ERR_FORMAT; // A directive naming nothing.
  }
  char * copy = gcu_allocator_malloc(allocator, length + 1);
  if (!copy) {
    return GMDL_ERR_OOM;
  }
  memcpy(copy, rest, length);
  copy[length] = '\0';
  *out = copy;
  return GMDL_OK;
}

/**
 * Record a `call` or `csh` statement without acting on it.
 *
 * Nothing is split, resolved or executed: see ::GMDL_Obj_Statement for why.
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
  char * copy = NULL;
  GMDL_Result copied = obj_copy_line_text(rest, allocator, &copy);
  if (copied != GMDL_OK) {
    return copied;
  }

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


// Every list of whole-line paths holds elements that are exactly one
// char[GMDL_OBJ_MAX_PATH_LENGTH] and nothing else, which is what lets
// obj_record_path() fill one through a plain buffer. Asserted rather than
// assumed: adding a field to any of them would otherwise make it copy the
// wrong number of bytes, silently.
_Static_assert(sizeof(GMDL_Obj_Mtllib) == GMDL_OBJ_MAX_PATH_LENGTH,
    "GMDL_Obj_Mtllib must be exactly its path");
_Static_assert(sizeof(GMDL_Obj_Maplib) == GMDL_OBJ_MAX_PATH_LENGTH,
    "GMDL_Obj_Maplib must be exactly its path");
_Static_assert(sizeof(GMDL_Obj_Render_Object) == GMDL_OBJ_MAX_PATH_LENGTH,
    "GMDL_Obj_Render_Object must be exactly its path");

/**
 * Read a whole-line path and append it to a list of them.
 *
 * `mtllib`, `maplib`, `shadow_obj` and `trace_obj` are read identically:
 * the whole line is the path, blanks at either end dropped, and a bare
 * directive names nothing and contributes no entry - appending an empty one
 * would put a file nobody asked for into the list and write it back out on
 * the dump. One reader rather than four copies, because the four copies had
 * already drifted once: `maplib` arrived with a line `mtllib` had needed and
 * no longer did.
 *
 * A path too long for the field is GMDL_ERR_LIMIT (3.9), since one cut at
 * 255 bytes names a different file, or none.
 *
 * @param rest The text after the directive.
 * @param paths The builder's array for that directive.
 * @param max The cap from GMDL_Obj_Options, or 0.
 * @return GMDL_OK - including for a bare directive - GMDL_ERR_LIMIT or
 *   GMDL_ERR_OOM.
 */
static GMDL_Result obj_record_path(
    const char * rest, GCU_Array * paths, size_t max) {
  char path[GMDL_OBJ_MAX_PATH_LENGTH];
  GMDL_Result named = gmdl_rest_of_line(rest, path, sizeof(path));
  if (named == GMDL_ERR_FORMAT) {
    return GMDL_OK; // A bare directive names nothing.
  }
  if (named != GMDL_OK) {
    return named;
  }
  if (gmdl_limit_reached(gcu_array_count(paths), max)) {
    return GMDL_ERR_LIMIT;
  }
  if (!gcu_array_append(paths, path)) {
    return GMDL_ERR_OOM;
  }
  return GMDL_OK;
}

/**
 * Read the `on` or `off` a render-attribute switch takes.
 *
 * Trailing text is ignored, as it is for `s` and for every number this
 * parser reads, so `bevel on please` is `on`. Anything that is neither word
 * is ::GMDL_ERR_FORMAT: the format defines two spellings and there is no
 * third reading to guess at, and a bare `bevel` names no value at all.
 */
static GMDL_Result obj_parse_on_off(const char * rest, bool * out) {
  if (gmdl_line_is(rest, "on", NULL)) {
    *out = true;
    return GMDL_OK;
  }
  if (gmdl_line_is(rest, "off", NULL)) {
    *out = false;
    return GMDL_OK;
  }
  return GMDL_ERR_FORMAT;
}

/** Whether a render state is the one a file starts in. */
static bool obj_render_is_default(const GMDL_Obj_Render_State * state) {
  return !state->bevel && !state->c_interp && !state->d_interp
      && state->lod == 0;
}

/**
 * Whether two render states hold the same attributes.
 *
 * Field by field rather than with memcmp(), because three bools and an
 * int32_t leave padding and memcmp() reads it - two states built the same
 * way can differ in bytes no field owns.
 */
static bool obj_render_same(
    const GMDL_Obj_Render_State * a, const GMDL_Obj_Render_State * b) {
  return a->bevel == b->bevel && a->c_interp == b->c_interp
      && a->d_interp == b->d_interp && a->lod == b->lod;
}

/**
 * Find the index elements should carry for @p wanted, appending a record if
 * the document has not put that combination in force before.
 *
 * The all-defaults state is named by -1 and never takes a record, so a
 * document mentioning none of the four directives - which is very nearly all
 * of them - carries no render states at all.
 *
 * @param states The builder's array.
 * @param wanted The attributes now in force.
 * @param max The cap from GMDL_Obj_Options, or 0.
 * @param out_index Receives the index, or -1.
 * @return GMDL_OK, GMDL_ERR_LIMIT or GMDL_ERR_OOM.
 */
static GMDL_Result obj_render_use(GCU_Array * states,
    const GMDL_Obj_Render_State * wanted, size_t max, int32_t * out_index) {
  if (obj_render_is_default(wanted)) {
    *out_index = -1;
    return GMDL_OK;
  }
  for (size_t i = 0; i < gcu_array_count(states); i++) {
    if (obj_render_same(
            (GMDL_Obj_Render_State *)gcu_array_at(states, i), wanted)) {
      *out_index = (int32_t)i;
      return GMDL_OK;
    }
  }
  if (gmdl_limit_reached(gcu_array_count(states), max)) {
    return GMDL_ERR_LIMIT;
  }
  if (!gcu_array_append(states, wanted)) {
    return GMDL_ERR_OOM;
  }
  *out_index = (int32_t)gcu_array_count(states) - 1;
  return GMDL_OK;
}

/**
 * Read up to @p max integers from @p rest, the way obj_take_floats() reads
 * numbers.
 *
 * Separate from gmdl_parse_int32(), which reads one and cannot say where it
 * stopped - `deg 3 2` and `step 5 5` each carry two, and a second call would
 * read the first number again.
 *
 * A number outside `int32_t` is ::GMDL_ERR_LIMIT rather than a saturated
 * value, which is the answer `s`, `lod` and `illum` already give: a value
 * the model cannot hold is a limit, and storing something else instead makes
 * the file unrecoverable.
 *
 * @param rest The text after the directive.
 * @param out Receives the numbers. At least @p max entries.
 * @param max How many to read before stopping.
 * @param out_count Receives how many were there, 0 to @p max.
 * @return ::GMDL_OK or ::GMDL_ERR_LIMIT.
 */
static GMDL_Result obj_take_int32s(
    const char * rest, int32_t * out, size_t max, size_t * out_count) {
  size_t taken = 0;
  while (taken < max) {
    while (*rest == ' ' || *rest == '\t') {
      rest++;
    }
    errno = 0;
    char * end = NULL;
    long value = strtol(rest, &end, 10);
    if (end == rest) {
      break;
    }
    if (errno == ERANGE || value > INT32_MAX || value < INT32_MIN) {
      return GMDL_ERR_LIMIT;
    }
    out[taken++] = (int32_t)value;
    rest = end;
  }
  *out_count = taken;
  return GMDL_OK;
}

/** The free-form state a file starts in: nothing set (3.19). */
static const GMDL_Obj_Freeform_State obj_freeform_state_none = {
    GMDL_OBJ_CSTYPE_NONE, false, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    GMDL_OBJ_CTECH_NONE, {0, 0}, GMDL_OBJ_STECH_NONE, {0, 0},
    GMDL_OBJ_MERGE_NONE, 0, 0, 0};

/**
 * Read the basis a `cstype` line names, and whether it said `rat`.
 *
 * The format defines five bases and `rat` is a prefix on any of them, not a
 * sixth. An unrecognised word is ::GMDL_ERR_FORMAT for the reason
 * obj_parse_on_off() gives: the format defines the spellings, and there is
 * no other reading to guess at.
 */
static GMDL_Result obj_parse_cstype(
    const char * rest, GMDL_Obj_Cstype * out_type, bool * out_rational) {
  bool rational = false;
  const char * after = NULL;
  if (gmdl_line_is(rest, "rat", &after)) {
    rational = true;
    rest = after;
  }
  GMDL_Obj_Cstype type = GMDL_OBJ_CSTYPE_NONE;
  if (!gmdl_cstype_from_name(rest, &type)) {
    return GMDL_ERR_FORMAT; // Including a bare `cstype` and `cstype rat`.
  }
  *out_type = type;
  *out_rational = rational;
  return GMDL_OK;
}

/**
 * Append one free-form control-point reference.
 *
 * @param builder The builder.
 * @param max The per-element cap from GMDL_Obj_Options, or 0.
 * @param count How many this element already has.
 * @return ::GMDL_OK, ::GMDL_ERR_LIMIT or ::GMDL_ERR_OOM.
 */
static GMDL_Result obj_freeform_vertex(obj_builder_t * b, size_t max,
    size_t count, int32_t vertex, int32_t texcoord, int32_t normal) {
  if (gmdl_limit_reached(count, max)) {
    return GMDL_ERR_LIMIT;
  }
  GMDL_Obj_Freeform_Vertex entry = {vertex, texcoord, normal};
  if (!gcu_array_append(&b->freeform_vertices, &entry)) {
    return GMDL_ERR_OOM;
  }
  return GMDL_OK;
}

/**
 * Read a `curv`, `curv2` or `surf` line and append the element it declares.
 *
 * The three differ in three ways and share everything else, which is why
 * they are one function: how many numbers of parameter range they carry (two,
 * none and four), which array their control points index, and whether a
 * reference may carry a texture coordinate and a normal.
 *
 * **The state is copied onto the element**, not referred to. See
 * ::GMDL_Obj_Freeform_State for why the free-form directives are held that
 * way where the render attributes are indexed.
 *
 * An element naming no control points at all is ::GMDL_ERR_FORMAT. So is one
 * whose parameter range is short: unlike a `vt`'s optional second number,
 * the specification gives `curv` and `surf` no shorter form, and a missing
 * `u1` would have to be invented.
 *
 * @param b The builder.
 * @param rest The text after the directive.
 * @param kind Which of the three it was.
 * @param state The free-form state in force.
 * @param limits The caps.
 * @param material The material index in force, or -1.
 * @param map The texture map index in force, or -1.
 * @param render The render state index in force, or -1.
 * @return ::GMDL_OK, ::GMDL_ERR_FORMAT, ::GMDL_ERR_LIMIT or ::GMDL_ERR_OOM.
 */
static GMDL_Result obj_record_freeform_element(obj_builder_t * b,
    const char * rest, GMDL_Obj_Freeform_Kind kind,
    const GMDL_Obj_Freeform_State * state, const GMDL_Obj_Options * limits,
    int32_t material, int32_t map, int32_t render) {
  if (gmdl_limit_reached(
          gcu_array_count(&b->freeforms), limits->max_freeforms)) {
    return GMDL_ERR_LIMIT;
  }

  GMDL_Obj_Freeform element;
  memset(&element, 0, sizeof(element));
  element.kind = kind;
  element.state = *state;
  element.material_index = material;
  element.map_index = map;
  element.render_index = render;
  element.start = gcu_array_count(&b->freeform_vertices);
  element.body_start = gcu_array_count(&b->freeform_bodies);

  const char * cursor = rest;
  size_t wanted = kind == GMDL_OBJ_SURFACE ? 4u
      : (kind == GMDL_OBJ_CURVE            ? 2u
                                           : 0u);
  // This refusal cannot currently be told apart from the one at the end of
  // the function, and that is a property of the format rather than a
  // redundancy to tidy away. For the range to be short, strtof() has to stop
  // at text that is not a number - and the token loop below then reads that
  // same text as a control point, where obj_parse_face_token() needs it to
  // begin with an integer, which it cannot. So a short range always reaches
  // `count == 0` as well, and both answer GMDL_ERR_FORMAT. Measured: a
  // mutation replacing this `return` with a `break` survives the whole
  // suite. It is kept because it refuses the line for the reason the line is
  // wrong, and because the two guards stop shadowing each other the moment
  // either rule changes.
  for (size_t i = 0; i < wanted; i++) {
    char * end = NULL;
    float value = strtof(cursor, &end);
    if (end == cursor) {
      return GMDL_ERR_FORMAT;
    }
    if (limits->reject_non_finite && !isfinite(value)) {
      return GMDL_ERR_FORMAT;
    }
    element.range[i] = value;
    cursor = end;
  }

  // Counts at this point in the file, which is what a relative index is
  // measured against - and `curv2` measures its against a different array
  // from the other two, which is the whole reason `vp` exists.
  size_t declared = kind == GMDL_OBJ_CURVE2
      ? gcu_array_count(&b->param_vertices)
      : gcu_array_count(&b->vertices);
  size_t texcoords = gcu_array_count(&b->texcoords);
  size_t normals = gcu_array_count(&b->normals);

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

    long long v = 0;
    long long vt = 0;
    long long vn = 0;
    if (!obj_parse_face_token(token, cursor, &v, &vt, &vn,
            limits->reject_extra_face_field)) {
      return GMDL_ERR_FORMAT;
    }
    // Only a `surf` reference has a `v/vt/vn` form. Accepting one on a
    // `curv` and dropping the extra fields would lose what the file said
    // while reporting success, which is the thing this library refuses to
    // do anywhere else.
    if (kind != GMDL_OBJ_SURFACE && (vt != 0 || vn != 0)) {
      return GMDL_ERR_FORMAT;
    }
    bool is_surface = kind == GMDL_OBJ_SURFACE;
    GMDL_Result added = obj_freeform_vertex(b, limits->max_face_indices,
        element.count, obj_index(v, declared),
        is_surface ? obj_index(vt, texcoords) : -1,
        is_surface ? obj_index(vn, normals) : -1);
    if (added != GMDL_OK) {
      return added;
    }
    element.count++;
  }

  if (element.count == 0) {
    return GMDL_ERR_FORMAT; // An element naming no control points.
  }
  if (!gcu_array_append(&b->freeforms, &element)) {
    return GMDL_ERR_OOM;
  }
  return GMDL_OK;
}

/**
 * Read one whitespace-delimited number from a line, as a float.
 *
 * **The whole token has to be the number.** `strtof()` stops at the first
 * character it cannot use and reports success for what it did read, which is
 * what the `bmat` and parameter-range loops rely on to find the end of a
 * list. The body statements cannot: `trim 0 1 2.5` would read the index as
 * `2`, leave `.5`, and take that as the next triple's `u0` - accepting a
 * line no file meant to write, silently, as a different line. So a token
 * that begins with a number and continues into something else is refused
 * here rather than resynchronised.
 *
 * @param cursor Advanced past the number on success, untouched otherwise.
 * @param out Receives the value.
 * @return Whether a whole token was a number.
 */
static bool obj_take_one_float(
    const char ** cursor, float * out, bool reject_non_finite) {
  const char * rest = *cursor;
  while (*rest == ' ' || *rest == '\t') {
    rest++;
  }
  char * end = NULL;
  float value = strtof(rest, &end);
  if (end == rest || (*end && *end != ' ' && *end != '\t')) {
    return false;
  }
  if (reject_non_finite && !isfinite(value)) {
    return false;
  }
  *out = value;
  *cursor = end;
  return true;
}

/**
 * Read one whitespace-delimited index from a line.
 *
 * Out of range saturates rather than wrapping, for the reason obj_index()
 * gives: an index too large to represent must stay out of range and never
 * arrive disguised as a valid one.
 *
 * @param cursor Advanced past the number on success, untouched otherwise.
 * @param out Receives the value, as written and not yet resolved.
 * @return Whether a whole token was an integer.
 */
static bool obj_take_one_index(const char ** cursor, long long * out) {
  const char * rest = *cursor;
  while (*rest == ' ' || *rest == '\t') {
    rest++;
  }
  char * end = NULL;
  long long value = strtoll(rest, &end, 10);
  if (end == rest || (*end && *end != ' ' && *end != '\t')) {
    return false;
  }
  *out = value;
  *cursor = end;
  return true;
}

/**
 * Read the numbers a `ctech` or `stech` technique calls for (3.18).
 *
 * **Exactly as many as the technique names**, no more and no fewer. This is
 * stricter than `deg` and `step`, which accept one number or two, and the
 * difference is in the format rather than in taste: those two directives
 * define both forms, so a reader must count to tell an absent second number
 * from a written one. A technique defines one arity, and a `ctech curv` with
 * one number has lost its `maxangle` somewhere - recording it as though the
 * file had said 0 would put a value into the model the document never wrote.
 *
 * Each number must be a whole token, for the reason obj_take_one_float()
 * gives: `ctech curv 1.5.2` would otherwise read as `1.5` and `0.2`, which
 * is a line the file did not write.
 *
 * @param cursor The text after the technique word.
 * @param arity How many numbers to read, 1 or 2.
 * @param out Receives them; the unused entry is left at 0.
 * @return ::GMDL_OK or ::GMDL_ERR_FORMAT.
 */
static GMDL_Result obj_take_technique_values(const char * cursor,
    size_t arity, float * out, bool reject_non_finite) {
  for (size_t i = 0; i < arity; i++) {
    if (!obj_take_one_float(&cursor, &out[i], reject_non_finite)) {
      return GMDL_ERR_FORMAT;
    }
  }
  return GMDL_OK;
}

/**
 * Read a `ctech` line into the free-form state (3.18).
 *
 * An unrecognised technique is ::GMDL_ERR_FORMAT for the reason
 * obj_parse_cstype() gives: the format defines the spellings and there is no
 * other reading to guess at. `ctech cparma` is one of them - that is a
 * surface technique, and accepting it here would record a curve as
 * approximated by a rule the format does not give curves.
 */
static GMDL_Result obj_parse_ctech(const char * rest,
    GMDL_Obj_Freeform_State * state, bool reject_non_finite) {
  GMDL_Obj_Ctech technique = GMDL_OBJ_CTECH_NONE;
  size_t arity = 0;
  const char * after = NULL;
  if (!gmdl_ctech_from_name(rest, &technique, &arity, &after)) {
    return GMDL_ERR_FORMAT;
  }
  float value[2] = {0, 0};
  GMDL_Result taken =
      obj_take_technique_values(after, arity, value, reject_non_finite);
  if (taken != GMDL_OK) {
    return taken;
  }
  state->ctech = technique;
  state->ctech_value[0] = value[0];
  state->ctech_value[1] = value[1];
  return GMDL_OK;
}

/** obj_parse_ctech() for `stech`. */
static GMDL_Result obj_parse_stech(const char * rest,
    GMDL_Obj_Freeform_State * state, bool reject_non_finite) {
  GMDL_Obj_Stech technique = GMDL_OBJ_STECH_NONE;
  size_t arity = 0;
  const char * after = NULL;
  if (!gmdl_stech_from_name(rest, &technique, &arity, &after)) {
    return GMDL_ERR_FORMAT;
  }
  float value[2] = {0, 0};
  GMDL_Result taken =
      obj_take_technique_values(after, arity, value, reject_non_finite);
  if (taken != GMDL_OK) {
    return taken;
  }
  state->stech = technique;
  state->stech_value[0] = value[0];
  state->stech_value[1] = value[1];
  return GMDL_OK;
}

/**
 * Read an `mg` line into the free-form state (3.18).
 *
 * `mg off` and `mg group [res]`. The resolution is optional and the reference
 * says so: it calls `res` "a required argument only when using merging
 * groups", and gives "a value of 0 or off" as the two ways to turn adjacency
 * detection off - so a line that disables merging carries no distance, and
 * refusing `mg 0` would reject a document that says exactly what the format
 * tells it to say. How
 * many numbers there were is recorded, because an absent `res` and a written
 * `res 0` are different lines (::GMDL_Obj_Freeform_State.merge_count).
 *
 * A group outside `int32_t` is ::GMDL_ERR_LIMIT, which is the answer `s` and
 * `lod` already give to a number the model cannot hold.
 */
static GMDL_Result obj_parse_mg(const char * rest,
    GMDL_Obj_Freeform_State * state, bool reject_non_finite) {
  if (gmdl_line_is(rest, "off", NULL)) {
    state->merge = GMDL_OBJ_MERGE_OFF;
    state->merge_group = 0;
    state->merge_resolution = 0;
    state->merge_count = 0;
    return GMDL_OK;
  }
  long long group = 0;
  const char * cursor = rest;
  if (!obj_take_one_index(&cursor, &group)) {
    return GMDL_ERR_FORMAT;
  }
  if (group > INT32_MAX || group < INT32_MIN) {
    return GMDL_ERR_LIMIT;
  }
  float resolution = 0;
  bool has_resolution =
      obj_take_one_float(&cursor, &resolution, reject_non_finite);
  if (!has_resolution) {
    // A second token that is not a number is a malformed line rather than
    // trailing text to ignore, which is what `v` does with it: there the
    // numbers are all required and anything after them is a fourth field the
    // format does not define, while here the resolution is optional, so
    // `mg 1 half` is a line that says a resolution and does not give one.
    // The blanks are skipped first because a line may end in them and that
    // is not text.
    while (*cursor == ' ' || *cursor == '\t') {
      cursor++;
    }
    if (*cursor != '\0') {
      return GMDL_ERR_FORMAT;
    }
  }
  state->merge = GMDL_OBJ_MERGE_ON;
  state->merge_group = (int32_t)group;
  state->merge_resolution = resolution;
  state->merge_count = has_resolution ? 2 : 1;
  return GMDL_OK;
}

/**
 * Read the entries of one body statement and append them.
 *
 * The five body statements carry three different payloads - `parm` floats,
 * `trim`, `hole` and `scrv` curve references, `sp` indices - and one loop
 * around them, because the part that would drift if written three times is
 * the appending, the cap and the count rather than the reading.
 *
 * A statement naming no entries at all is ::GMDL_ERR_FORMAT, as a `curv`
 * naming no control points is: `trim` with nothing after it describes no
 * loop, and recording an empty one would put a record in the model that
 * corresponds to nothing in the file.
 *
 * @param b The builder.
 * @param cursor The text after the directive.
 * @param kind Which statement it is; selects the array and the cap.
 * @param limits The caps.
 * @param declared What a relative index is measured against: the `vp` count
 *   for `sp`, the number of `curv2` elements so far for the other three, and
 *   unused for `parm`.
 * @param out_start Receives the first entry's index in its array.
 * @param out_count Receives how many.
 * @return ::GMDL_OK, ::GMDL_ERR_FORMAT, ::GMDL_ERR_LIMIT or ::GMDL_ERR_OOM.
 */
static GMDL_Result obj_take_body_entries(obj_builder_t * b,
    const char * cursor, GMDL_Obj_Body_Kind kind, const GMDL_Obj_Options * limits,
    size_t declared, size_t * out_start, size_t * out_count) {
  GCU_Array * into = &b->curve_refs;
  size_t cap = limits->max_curve_refs;
  if (kind == GMDL_OBJ_BODY_PARM_U || kind == GMDL_OBJ_BODY_PARM_V) {
    into = &b->parm_values;
    cap = limits->max_parm_values;
  }
  else if (kind == GMDL_OBJ_BODY_SP) {
    into = &b->special_points;
    cap = limits->max_special_points;
  }

  *out_start = gcu_array_count(into);
  size_t count = 0;
  for (;;) {
    while (*cursor == ' ' || *cursor == '\t') {
      cursor++;
    }
    if (!*cursor) {
      break;
    }

    GMDL_Obj_Curve_Ref ref;
    float value = 0.0f;
    int32_t point = 0;
    long long index = 0;
    void * entry = NULL;
    // Branching on the array rather than on the kind again, so that what is
    // read and what it is appended to cannot come apart: a kind added to the
    // switch above and forgotten here would read curve references into the
    // parameter values without either decision noticing the other.
    if (into == &b->parm_values) {
      if (!obj_take_one_float(&cursor, &value, limits->reject_non_finite)) {
        return GMDL_ERR_FORMAT;
      }
      entry = &value;
    }
    else if (into == &b->special_points) {
      if (!obj_take_one_index(&cursor, &index)) {
        return GMDL_ERR_FORMAT;
      }
      point = obj_index(index, declared);
      entry = &point;
    }
    else {
      // A triple that stops short is refused rather than kept as far as it
      // got: two of the three numbers name a range with no curve in it.
      if (!obj_take_one_float(&cursor, &ref.u0, limits->reject_non_finite)
          || !obj_take_one_float(&cursor, &ref.u1, limits->reject_non_finite)
          || !obj_take_one_index(&cursor, &index)) {
        return GMDL_ERR_FORMAT;
      }
      ref.curve2d = obj_index(index, declared);
      entry = &ref;
    }

    if (gmdl_limit_reached(gcu_array_count(into), cap)) {
      return GMDL_ERR_LIMIT;
    }
    if (!gcu_array_append(into, entry)) {
      return GMDL_ERR_OOM;
    }
    count++;
  }

  if (count == 0) {
    return GMDL_ERR_FORMAT; // A body statement naming nothing.
  }
  *out_count = count;
  return GMDL_OK;
}

/**
 * Read one body statement and attach it to the element that is open.
 *
 * **A body statement outside an element is ::GMDL_ERR_FORMAT.** It describes
 * the element it stands in, so there is nothing to attach it to and nothing
 * honest to do with it: recording it against the previous element would
 * change which patch is trimmed, and dropping it would lose what the file
 * said while reporting success.
 *
 * @param b The builder.
 * @param rest The text after the directive.
 * @param kind Which statement it is.
 * @param open The open element's index in the builder, or SIZE_MAX.
 * @param limits The caps.
 * @param declared What a relative index is measured against.
 * @return ::GMDL_OK, ::GMDL_ERR_FORMAT, ::GMDL_ERR_LIMIT or ::GMDL_ERR_OOM.
 */
static GMDL_Result obj_record_body(obj_builder_t * b, const char * rest,
    GMDL_Obj_Body_Kind kind, size_t open, const GMDL_Obj_Options * limits,
    size_t declared) {
  if (open == SIZE_MAX) {
    return GMDL_ERR_FORMAT;
  }
  if (gmdl_limit_reached(
          gcu_array_count(&b->freeform_bodies), limits->max_freeform_bodies)) {
    return GMDL_ERR_LIMIT;
  }

  GMDL_Obj_Freeform_Body body;
  memset(&body, 0, sizeof(body));
  body.kind = kind;
  GMDL_Result taken = obj_take_body_entries(
      b, rest, kind, limits, declared, &body.start, &body.count);
  if (taken != GMDL_OK) {
    return taken;
  }
  if (!gcu_array_append(&b->freeform_bodies, &body)) {
    return GMDL_ERR_OOM;
  }
  // The element lives in a different array from the one just appended to, so
  // this pointer cannot have been invalidated by the append above.
  GMDL_Obj_Freeform * element =
      (GMDL_Obj_Freeform *)gcu_array_at(&b->freeforms, open);
  element->body_count++;
  return GMDL_OK;
}

static GMDL_Result obj_load_pinned(GMDL_Stream * stream,
    const GMDL_Obj_Options * limits, const GMDL_Allocator * allocator,
    GMDL_Obj ** out_obj) {
  if (!out_obj) {
    return GMDL_ERR_INVALID;
  }
  *out_obj = NULL;
  if (!stream) {
    return GMDL_ERR_INVALID;
  }

  GMDL_Obj_Options defaults;
  if (!limits) {
    gmdl_obj_options_default(&defaults);
    limits = &defaults;
  }

  // Zero is not "unlimited" here - see GMDL_Obj_Options.max_line_length - because
  // this buffer is allocated before the first line is read.
  size_t line_size = limits->max_line_length ? limits->max_line_length
                                             : GMDL_DEFAULT_MAX_LINE_LENGTH;
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

  long current_group = -1;          // Index of the active group.
  int32_t current_material = -1;    // Material set by the last "usemtl".
  // Texture map set by the last "usemap"; -1 is "none", which is both the
  // state a file starts in and what "usemap off" returns it to.
  int32_t current_map = -1;
  // The render attributes in force, and the index elements carry for them.
  // Both are kept: the values are what the next directive modifies, the
  // index is what an element records. -1 is the all-defaults state.
  GMDL_Obj_Render_State current_render = {false, false, false, 0};
  int32_t current_render_index = -1;
  // The free-form state in force, copied onto each element as it is
  // declared (3.19). Nothing indexes it, so there is no second variable here
  // the way there is for the render attributes.
  GMDL_Obj_Freeform_State current_freeform = obj_freeform_state_none;
  // Which free-form element a body statement belongs to: the last `curv`,
  // `curv2` or `surf`, until an `end` or the next element closes it.
  // SIZE_MAX is "none open", which is where a file starts and where `end`
  // returns it - and the value a body statement is refused against (3.19).
  size_t open_element = SIZE_MAX;
  // How many elements of each kind have been declared. `trim`, `hole`,
  // `scrv` and `con` number their references within a kind rather than
  // across the three, so these are what a relative index is measured
  // against, and neither is the length of the `freeforms` array.
  size_t curve2_count = 0;
  size_t surface_count = 0;
  // Smoothing group set by the last "s"; 0 is the format's own default, so a
  // file with no "s" line leaves every face at 0 and writes none back.
  int32_t current_smoothing = 0;

  GMDL_Line_Reader reader;
  gmdl_line_reader_init(&reader, stream, line, line_size);
  reader.keep_byte_order_mark = limits->keep_byte_order_mark;
  reader.keep_leading_whitespace = limits->keep_leading_whitespace;
  reader.keep_inline_comments = limits->keep_inline_comments;
  reader.no_line_continuation = limits->no_line_continuation;
  reader.join_before_comment = limits->join_before_comment;
  reader.reject_vertex_continuation = limits->reject_vertex_continuation;
  reader.reject_face_comment = limits->reject_face_comment;
  reader.break_group_continuation = limits->break_group_continuation;

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
      // Four or five numbers is the specification's `w`. Six or more is the
      // `r g b` extension, and the colour is fields four to six - which is
      // what Blender does, measured on four, five, six and seven numbers.
      // The two forms occupy the same fields, so a line carries one of them.
      // A rational curve's weight used to be read and dropped; `cstype rat`
      // records that the curve is rational, and without `w` the control
      // point that makes it so is gone (3.1).
      float number[6];
      size_t count = obj_take_floats(rest, number, 6);
      // Omitting a short or non-finite vertex leaves no index for it.
      // Padding and rejecting are the other readings; omitting wins when
      // more than one is set.
      if (limits->omit_short_vertex && count < 3) {
        continue;
      }
      if (limits->omit_non_finite_vertex) {
        bool finite = true;
        size_t checked = count < 3 ? count : 3;
        for (size_t i = 0; i < checked; i++) {
          if (!isfinite(number[i])) {
            finite = false;
          }
        }
        if (!finite) {
          continue;
        }
      }
      // Substitution loses to a rejection: both set still fails the file.
      if (!limits->reject_non_finite) {
        obj_zero_non_finite(
            number, count, limits->non_finite_becomes_zero);
      }
      if (obj_rejected_non_finite(number, count, limits->reject_non_finite)) {
        result = GMDL_ERR_FORMAT;
        goto cleanup;
      }
      if (count < 3) {
        // One or two numbers are a vertex with the missing coordinates
        // padded to zero, when the caller asked for that reading. No
        // numbers is still not a vertex. The count is left as read, so the
        // padded line is not then taken as a weight or a colour.
        if (!limits->accept_short_vertex || count == 0) {
          result = GMDL_ERR_FORMAT;
          goto cleanup;
        }
        if (count < 2) {
          number[1] = 0.0f;
        }
        number[2] = 0.0f;
      }
      GMDL_Obj_Vertex v = {number[0], number[1], number[2]};
      GMDL_Obj_Color color = obj_color_absent;
      GMDL_Obj_Weight weight = obj_weight_absent;
      if (count >= 6) {
        color = (GMDL_Obj_Color){number[3], number[4], number[5], true};
      }
      else if (count >= 4) {
        weight.w = number[3];
        weight.present = true;
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
      if ((weight.present || gcu_array_count(&builder.weights) != 0)
          && !obj_weight_append(&builder, &weight)) {
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
      if (!limits->reject_non_finite) {
        obj_zero_non_finite(number, count, limits->non_finite_becomes_zero);
      }
      if (count < 1 || (limits->reject_short_texcoord && count < 2)
          || obj_rejected_non_finite(
              number, count, limits->reject_non_finite)) {
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
      size_t count = obj_take_floats(rest, number, 3);
      if (!limits->reject_non_finite) {
        obj_zero_non_finite(number, count, limits->non_finite_becomes_zero);
      }
      if (count < 3
          || obj_rejected_non_finite(
              number, count, limits->reject_non_finite)) {
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
    else if (gmdl_line_is(line_text, "vp", &rest)) {
      // `vp u [v] [w]`: a control point in the parameter space of a curve or
      // a surface (3.19), not a point in the model's space, and indexed
      // separately from `v`. Only `u` is required - a `curv2` control point
      // is one-dimensional - and the two that may be absent take the
      // format's own defaults rather than being left unset, so a consumer
      // that ignores `count` still reads something.
      //
      // How many the line carried is recorded, because it is the only thing
      // that distinguishes a curve's control point from a surface's, and the
      // dump cannot write the statement back without it.
      float number[3];
      size_t count = obj_take_floats(rest, number, 3);
      if (!limits->reject_non_finite) {
        obj_zero_non_finite(number, count, limits->non_finite_becomes_zero);
      }
      if (count < 1
          || obj_rejected_non_finite(
              number, count, limits->reject_non_finite)) {
        result = GMDL_ERR_FORMAT;
        goto cleanup;
      }
      GMDL_Obj_Param_Vertex vp = {number[0], count >= 2 ? number[1] : 0.0f,
          count >= 3 ? number[2] : 1.0f, (int32_t)count};
      if (gmdl_limit_reached(gcu_array_count(&builder.param_vertices),
              limits->max_param_vertices)) {
        result = GMDL_ERR_LIMIT;
        goto cleanup;
      }
      if (!gcu_array_append(&builder.param_vertices, &vp)) {
        result = GMDL_ERR_OOM;
        goto cleanup;
      }
    }
    // The free-form state directives (3.19). Each is state exactly as
    // `usemtl` is, and together they say how the control points a `curv`,
    // `curv2` or `surf` names are to be read.
    else if (gmdl_line_is(line_text, "cstype", &rest)) {
      GMDL_Result parsed = obj_parse_cstype(
          rest, &current_freeform.type, &current_freeform.rational);
      if (parsed != GMDL_OK) {
        result = parsed;
        goto cleanup;
      }
    }
    else if (gmdl_line_is(line_text, "deg", &rest)) {
      // `deg degu [degv]`: the second is a surface's, and a curve states
      // one. How many the line carried is recorded rather than marked with
      // a sentinel value, because every int32_t is a number some file can
      // write: -1 was the sentinel at first and `step -1 1` broke the round
      // trip (3.19).
      int32_t number[2];
      size_t count = 0;
      GMDL_Result parsed = obj_take_int32s(rest, number, 2, &count);
      if (parsed != GMDL_OK) {
        result = parsed;
        goto cleanup;
      }
      if (count < 1) {
        result = GMDL_ERR_FORMAT;
        goto cleanup;
      }
      current_freeform.degree_u = number[0];
      current_freeform.degree_v = count >= 2 ? number[1] : 0;
      current_freeform.degree_count = (int32_t)count;
    }
    else if (gmdl_line_is(line_text, "step", &rest)) {
      int32_t number[2];
      size_t count = 0;
      GMDL_Result parsed = obj_take_int32s(rest, number, 2, &count);
      if (parsed != GMDL_OK) {
        result = parsed;
        goto cleanup;
      }
      if (count < 1) {
        result = GMDL_ERR_FORMAT;
        goto cleanup;
      }
      current_freeform.step_u = number[0];
      current_freeform.step_v = count >= 2 ? number[1] : 0;
      current_freeform.step_count = (int32_t)count;
    }
    else if (gmdl_line_is(line_text, "bmat", &rest)) {
      // `bmat u|v <values>`. How many values there are is `(deg + 1)`
      // squared, which this does not check: the degree is set by a separate
      // directive that a file may state after this one, and refusing the
      // line here would reject a document whose directives are merely in an
      // order this parser did not expect.
      bool is_v = false;
      const char * after = NULL;
      if (gmdl_line_is(rest, "u", &after)) {
        is_v = false;
      }
      else if (gmdl_line_is(rest, "v", &after)) {
        is_v = true;
      }
      else {
        result = GMDL_ERR_FORMAT; // Including a bare `bmat`.
        goto cleanup;
      }

      size_t start = gcu_array_count(&builder.basis_values);
      size_t count = 0;
      const char * cursor = after;
      for (;;) {
        char * end = NULL;
        float value = strtof(cursor, &end);
        if (end == cursor) {
          break;
        }
        if (limits->reject_non_finite && !isfinite(value)) {
          result = GMDL_ERR_FORMAT;
          goto cleanup;
        }
        cursor = end;
        if (gmdl_limit_reached(gcu_array_count(&builder.basis_values),
                limits->max_basis_values)) {
          result = GMDL_ERR_LIMIT;
          goto cleanup;
        }
        if (!gcu_array_append(&builder.basis_values, &value)) {
          result = GMDL_ERR_OOM;
          goto cleanup;
        }
        count++;
      }
      if (count == 0) {
        result = GMDL_ERR_FORMAT;
        goto cleanup;
      }
      if (is_v) {
        current_freeform.basis_v_start = start;
        current_freeform.basis_v_count = count;
      }
      else {
        current_freeform.basis_u_start = start;
        current_freeform.basis_u_count = count;
      }
    }
    // The free-form elements themselves. `curv2` is tested first only so the
    // pair reads in the order the specification lists them; gmdl_line_is()
    // ends a directive at whitespace, so `curv` does not match `curv2 1 2`
    // either way.
    else if (gmdl_line_is(line_text, "curv2", &rest)) {
      GMDL_Result recorded = obj_record_freeform_element(&builder, rest,
          GMDL_OBJ_CURVE2, &current_freeform, limits, current_material,
          current_map, current_render_index);
      if (recorded != GMDL_OK) {
        result = recorded;
        goto cleanup;
      }
      // Declaring an element closes whatever was open, which is what lets a
      // file that omits `end` still parse the way 3.19 says it does.
      open_element = gcu_array_count(&builder.freeforms) - 1;
      curve2_count++;
    }
    else if (gmdl_line_is(line_text, "curv", &rest)) {
      GMDL_Result recorded = obj_record_freeform_element(&builder, rest,
          GMDL_OBJ_CURVE, &current_freeform, limits, current_material,
          current_map, current_render_index);
      if (recorded != GMDL_OK) {
        result = recorded;
        goto cleanup;
      }
      // Declaring an element closes whatever was open, which is what lets a
      // file that omits `end` still parse the way 3.19 says it does.
      open_element = gcu_array_count(&builder.freeforms) - 1;
    }
    else if (gmdl_line_is(line_text, "surf", &rest)) {
      GMDL_Result recorded = obj_record_freeform_element(&builder, rest,
          GMDL_OBJ_SURFACE, &current_freeform, limits, current_material,
          current_map, current_render_index);
      if (recorded != GMDL_OK) {
        result = recorded;
        goto cleanup;
      }
      // Declaring an element closes whatever was open, which is what lets a
      // file that omits `end` still parse the way 3.19 says it does.
      open_element = gcu_array_count(&builder.freeforms) - 1;
      surface_count++;
    }
    // `end` closes the element a body statement would attach to, and is not
    // recorded: a closed element and an unclosed one hold the same data, and
    // what `end` decides is answered in the element's body span by the time
    // parsing finishes (3.19). One with no element open is ignored rather
    // than refused - there is nothing for it to lose.
    else if (gmdl_line_is(line_text, "end", &rest)) {
      open_element = SIZE_MAX;
    }
    // The body statements (3.19). Each describes the element it stands in,
    // so each is refused outside one.
    else if (gmdl_line_is(line_text, "parm", &rest)) {
      // `parm u|v p1 p2 ...`. Unlike `bmat`, whose direction picks which of
      // two spans on the state to fill, this one picks which kind of record
      // the line becomes: a body statement is one record per line, so two
      // `parm u` lines stay two records rather than one replacing the other.
      GMDL_Obj_Body_Kind kind = GMDL_OBJ_BODY_PARM_U;
      const char * after = NULL;
      if (gmdl_line_is(rest, "u", &after)) {
        kind = GMDL_OBJ_BODY_PARM_U;
      }
      else if (gmdl_line_is(rest, "v", &after)) {
        kind = GMDL_OBJ_BODY_PARM_V;
      }
      else {
        result = GMDL_ERR_FORMAT; // Including a bare `parm`.
        goto cleanup;
      }
      GMDL_Result recorded = obj_record_body(
          &builder, after, kind, open_element, limits, 0);
      if (recorded != GMDL_OK) {
        result = recorded;
        goto cleanup;
      }
    }
    else if (gmdl_line_is(line_text, "trim", &rest)
        || gmdl_line_is(line_text, "hole", &rest)
        || gmdl_line_is(line_text, "scrv", &rest)) {
      GMDL_Obj_Body_Kind kind = GMDL_OBJ_BODY_SCRV;
      if (gmdl_line_is(line_text, "trim", NULL)) {
        kind = GMDL_OBJ_BODY_TRIM;
      }
      else if (gmdl_line_is(line_text, "hole", NULL)) {
        kind = GMDL_OBJ_BODY_HOLE;
      }
      GMDL_Result recorded = obj_record_body(
          &builder, rest, kind, open_element, limits, curve2_count);
      if (recorded != GMDL_OK) {
        result = recorded;
        goto cleanup;
      }
    }
    else if (gmdl_line_is(line_text, "sp", &rest)) {
      // A special point is a point in parameter space whatever kind of
      // element it belongs to, so this counts into `vp` even on a `curv` -
      // which is the one place a free-form reference does not change array
      // with the element's kind.
      GMDL_Result recorded = obj_record_body(&builder, rest,
          GMDL_OBJ_BODY_SP, open_element, limits,
          gcu_array_count(&builder.param_vertices));
      if (recorded != GMDL_OK) {
        result = recorded;
        goto cleanup;
      }
    }
    // `con` is the one free-form directive that is neither state nor a body
    // statement: it stands at file level and names its two surfaces, so it
    // is recorded whether or not an element is open and does not close one.
    else if (gmdl_line_is(line_text, "con", &rest)) {
      if (gmdl_limit_reached(gcu_array_count(&builder.connections),
              limits->max_connections)) {
        result = GMDL_ERR_LIMIT;
        goto cleanup;
      }
      GMDL_Obj_Connection connection;
      memset(&connection, 0, sizeof(connection));
      GMDL_Obj_Connection_End * ends[2] = {&connection.a, &connection.b};
      const char * cursor = rest;
      bool complete = true;
      for (size_t i = 0; i < 2 && complete; i++) {
        long long surface = 0;
        long long curve = 0;
        complete = obj_take_one_index(&cursor, &surface)
            && obj_take_one_float(
                &cursor, &ends[i]->curve.u0, limits->reject_non_finite)
            && obj_take_one_float(
                &cursor, &ends[i]->curve.u1, limits->reject_non_finite)
            && obj_take_one_index(&cursor, &curve);
        if (complete) {
          ends[i]->surface = obj_index(surface, surface_count);
          ends[i]->curve.curve2d = obj_index(curve, curve2_count);
        }
      }
      // All eight numbers or none. A `con` naming one surface and half of
      // the other describes no join, and there is no shorter conforming
      // form for it to be.
      if (!complete) {
        result = GMDL_ERR_FORMAT;
        goto cleanup;
      }
      if (!gcu_array_append(&builder.connections, &connection)) {
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
      face.map_index = current_map;
      face.render_index = current_render_index;
      face.smoothing_group = current_smoothing;
      face.overflow = NULL;

      // Vertices past the fourth go into an overflow array, which is handed to
      // the face at the end of the line.
      //
      // The OOM arm below cannot run today and is kept anyway. A capacity of
      // zero allocates nothing, so gcu_array_create_in_place() can only fail
      // here on a NULL array or a zero element size, neither of which a
      // literal `&overflow` and a `sizeof` can be. The allocation-failure
      // sweep in tests/unit/test_allocator.cpp reaches every other arm in
      // this function and reports this one uncovered; that is the reason,
      // rather than a gap. Giving the array a starting capacity would make it
      // reachable again, which is why the check stays.
      GCU_Array overflow;
      if (!gcu_array_create_in_place(
              &overflow, sizeof(GMDL_Obj_Face_Overflow), 0, allocator)) {
        result = GMDL_ERR_OOM;
        goto cleanup;
      }

      bool skip_face = false;
      const char * cursor = rest;
      while (*cursor && !skip_face) {
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

        long long v = 0;
        long long vt = 0;
        long long vn = 0;
        if (!obj_parse_face_token(token, cursor, &v, &vt, &vn,
                limits->reject_extra_face_field)) {
          gcu_array_destroy_in_place(&overflow);
          // A token that does not parse drops that face when the caller
          // asked to omit it; otherwise the file fails. `continue` here
          // would resume this loop, which has just destroyed `overflow`.
          if (limits->omit_malformed_faces) {
            skip_face = true;
            break;
          }
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
      if (skip_face) {
        continue;
      }

      // Trim before handing it over: this block outlives the parse and there
      // may be one per face.
      (void)gcu_array_shrink_to_fit(&overflow);
      face.overflow = (GMDL_Obj_Face_Overflow *)gcu_array_steal(&overflow, NULL);
      gcu_array_destroy_in_place(&overflow);

      // A face outside the triangle/quad set, or one that names a missing
      // vertex, can be left out. A quad can then be stored as two triangles.
      if (limits->omit_non_triangle_quad_faces
          && face.count != 3 && face.count != 4) {
        gcu_allocator_free(allocator, face.overflow);
        continue;
      }
      if (limits->omit_unresolved_faces) {
        bool missing = false;
        for (size_t i = 0; i < face.count && !missing; i++) {
          int32_t index =
              i < 4 ? face.vertex[i] : face.overflow[i - 4].vertex;
          if (index < 0 || (size_t)index >= vertex_count) {
            missing = true;
          }
        }
        if (missing) {
          gcu_allocator_free(allocator, face.overflow);
          continue;
        }
      }
      if (limits->triangulate_quads && face.count == 4) {
        static const int corner[2][3] = {{0, 1, 2}, {2, 3, 0}};
        for (int tri = 0; tri < 2; tri++) {
          if (tri == 1
              && gmdl_limit_reached(
                  obj_element_count(&builder), limits->max_faces)) {
            gcu_allocator_free(allocator, face.overflow);
            result = GMDL_ERR_LIMIT;
            goto cleanup;
          }
          GMDL_Obj_Face half;
          memset(&half, 0, sizeof(half));
          half.count = 3;
          half.material_index = face.material_index;
          half.map_index = face.map_index;
          half.render_index = face.render_index;
          half.smoothing_group = face.smoothing_group;
          for (int k = 0; k < 3; k++) {
            int c = corner[tri][k];
            half.vertex[k] = face.vertex[c];
            half.texcoord[k] = face.texcoord[c];
            half.normal[k] = face.normal[c];
          }
          if (!gcu_array_append(&builder.faces, &half)) {
            gcu_allocator_free(allocator, face.overflow);
            result = GMDL_ERR_OOM;
            goto cleanup;
          }
          if (current_group >= 0) {
            for (size_t gi = (size_t)current_group;
                 gi < gcu_array_count(&builder.groups); gi++) {
              GMDL_Obj_Group * group =
                  (GMDL_Obj_Group *)gcu_array_at(&builder.groups, gi);
              group->face_count++;
            }
          }
        }
        gcu_allocator_free(allocator, face.overflow);
        continue;
      }

      if (!gcu_array_append(&builder.faces, &face)) {
        // The face never reached the array, so its overflow will not be freed
        // along with the rest.
        gcu_allocator_free(allocator, face.overflow);
        result = GMDL_ERR_OOM;
        goto cleanup;
      }
      // Every name on the open statement shares the faces. `g a b` is two
      // groups, and incrementing only the last would leave `a` empty.
      if (current_group >= 0) {
        for (size_t gi = (size_t)current_group;
             gi < gcu_array_count(&builder.groups); gi++) {
          GMDL_Obj_Group * group =
              (GMDL_Obj_Group *)gcu_array_at(&builder.groups, gi);
          group->face_count++;
        }
      }
    }
    else if (gmdl_line_is(line_text, "g", &rest)
        || gmdl_line_is(line_text, "o", &rest)) {
      // Which of the two it was, so the dump can write back the spelling the
      // file used. They do not mean the same thing to the tools that write
      // them, and they do not parse the same way either (3.6).
      bool is_object = line_text[0] == 'o';
      // FreeCAD raises SystemError on `g a\`. `g a\\` is a name and the
      // face on the next line survives, and `o a\` is not the error, so
      // this is an unpaired backslash at the end of a `g` line only.
      if (limits->reject_unpaired_group_backslash && !is_object
          && line_text[0] != '\0') {
        size_t length = strlen(line_text);
        size_t slashes = 0;
        while (length > slashes && line_text[length - 1 - slashes] == '\\') {
          slashes++;
        }
        if (slashes % 2 == 1) {
          result = GMDL_ERR_FORMAT;
          goto cleanup;
        }
      }
      // A bare directive names the default group. A name too long for the
      // field is refused rather than cut, because the first 127 bytes of a
      // name name something else (3.9).
      size_t first = gcu_array_count(&builder.groups);
      // `o` is one name, spaces included. `g` is one name per word unless
      // the caller asked for Blender and VTK's reading, where `g a b` is
      // the single name `a b`.
      if (is_object || limits->group_line_is_one_name) {
        // `o` takes one name, and the name may contain spaces. Splitting it
        // would rename the object.
        char name[GMDL_OBJ_MAX_NAME_LENGTH];
        GMDL_Result named = gmdl_rest_of_line(rest, name, sizeof(name));
        if (named == GMDL_ERR_FORMAT) {
          memcpy(name, "default", sizeof("default"));
        }
        else if (named != GMDL_OK) {
          result = named;
          goto cleanup;
        }
        result = obj_append_group(
            &builder, name, is_object, false, limits->max_groups);
        if (result != GMDL_OK) {
          goto cleanup;
        }
      }
      else {
        // `g a b` is two groups (3.6). The specification says so, and
        // keeping the line whole was the reading that postponed the
        // decision. Each name shares the face range; `joined` is what lets
        // the dump write them back as one line.
        const char * cursor = rest;
        bool any = false;
        bool joined = false;
        while (*cursor) {
          while (*cursor == ' ' || *cursor == '\t') {
            cursor++;
          }
          if (!*cursor) {
            break;
          }
          const char * start = cursor;
          while (*cursor && *cursor != ' ' && *cursor != '\t') {
            cursor++;
          }
          size_t length = (size_t)(cursor - start);
          if (length >= GMDL_OBJ_MAX_NAME_LENGTH) {
            result = GMDL_ERR_LIMIT;
            goto cleanup;
          }
          char name[GMDL_OBJ_MAX_NAME_LENGTH];
          memcpy(name, start, length);
          name[length] = '\0';
          result = obj_append_group(
              &builder, name, false, joined, limits->max_groups);
          if (result != GMDL_OK) {
            goto cleanup;
          }
          joined = true;
          any = true;
        }
        if (!any) {
          result = obj_append_group(
              &builder, "default", false, false, limits->max_groups);
          if (result != GMDL_OK) {
            goto cleanup;
          }
        }
      }
      current_group = (long)first;
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
      element.map_index = current_map;
      element.render_index = current_render_index;

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

        long long v = 0;
        long long vt = 0;
        long long vn = 0;
        // The same token grammar as a face, and for the same reason: a file
        // that writes "1//2" on an l line is using a spelling the format
        // does not give lines, and refusing it would reject a file every
        // other reader accepts. The normal is read and dropped - a polyline
        // has nothing to do with one.
        if (!obj_parse_face_token(token, cursor, &v, &vt, &vn,
                limits->reject_extra_face_field)) {
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
        long long v = strtoll(cursor, &end, 10);
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
        stored->map_index = current_map;
        stored->render_index = current_render_index;
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
    // The render attributes (3.16). Each is state, like `usemtl`, and each
    // takes effect for the elements that follow it.
    else if (gmdl_line_is(line_text, "bevel", &rest)) {
      GMDL_Result parsed = obj_parse_on_off(rest, &current_render.bevel);
      if (parsed != GMDL_OK) {
        result = parsed;
        goto cleanup;
      }
      GMDL_Result used = obj_render_use(&builder.render_states,
          &current_render, limits->max_render_states, &current_render_index);
      if (used != GMDL_OK) {
        result = used;
        goto cleanup;
      }
    }
    else if (gmdl_line_is(line_text, "c_interp", &rest)) {
      GMDL_Result parsed = obj_parse_on_off(rest, &current_render.c_interp);
      if (parsed != GMDL_OK) {
        result = parsed;
        goto cleanup;
      }
      GMDL_Result used = obj_render_use(&builder.render_states,
          &current_render, limits->max_render_states, &current_render_index);
      if (used != GMDL_OK) {
        result = used;
        goto cleanup;
      }
    }
    else if (gmdl_line_is(line_text, "d_interp", &rest)) {
      GMDL_Result parsed = obj_parse_on_off(rest, &current_render.d_interp);
      if (parsed != GMDL_OK) {
        result = parsed;
        goto cleanup;
      }
      GMDL_Result used = obj_render_use(&builder.render_states,
          &current_render, limits->max_render_states, &current_render_index);
      if (used != GMDL_OK) {
        result = used;
        goto cleanup;
      }
    }
    else if (gmdl_line_is(line_text, "lod", &rest)) {
      // Documented as 0 to 100 and kept as written, in or out of that range,
      // for the reason section 1 gives: a value this parser corrected would
      // leave the file unrecoverable. An integer too wide for the field is
      // GMDL_ERR_LIMIT, as it is for `s`.
      int32_t value = 0;
      GMDL_Result parsed = gmdl_parse_int32(rest, &value);
      if (parsed != GMDL_OK) {
        result = parsed;
        goto cleanup;
      }
      current_render.lod = value;
      GMDL_Result used = obj_render_use(&builder.render_states,
          &current_render, limits->max_render_states, &current_render_index);
      if (used != GMDL_OK) {
        result = used;
        goto cleanup;
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
    else if (gmdl_line_is(line_text, "usemap", &rest)) {
      // `usemap` is `usemtl` for texture maps (3.15): the whole line is the
      // name, a bare one is GMDL_ERR_FORMAT, and an over-long one is
      // GMDL_ERR_LIMIT. The one thing it has that `usemtl` does not is a
      // spelling for "none".
      char map_name[GMDL_OBJ_MAX_NAME_LENGTH];
      GMDL_Result named = gmdl_rest_of_line(rest, map_name, sizeof(map_name));
      if (named != GMDL_OK) {
        result = named;
        goto cleanup;
      }

      if (strcmp(map_name, "off") == 0) {
        // The format reserves the word, so a map really called "off" cannot
        // be named - the same trade `s off` already makes for smoothing.
        current_map = -1;
      }
      else {
        int32_t mapped = -1;
        for (size_t i = 0; i < gcu_array_count(&builder.map_mappings); i++) {
          GMDL_Obj_Map_Mapping * mapping =
              (GMDL_Obj_Map_Mapping *)gcu_array_at(&builder.map_mappings, i);
          if (strcmp(mapping->name, map_name) == 0) {
            mapped = mapping->index;
            break;
          }
        }
        if (mapped < 0) {
          if (gmdl_limit_reached(
                  gcu_array_count(&builder.map_mappings), limits->max_maps)) {
            result = GMDL_ERR_LIMIT;
            goto cleanup;
          }
          GMDL_Obj_Map_Mapping * mapping =
              (GMDL_Obj_Map_Mapping *)gcu_array_emplace(&builder.map_mappings);
          if (!mapping) {
            result = GMDL_ERR_OOM;
            goto cleanup;
          }
          memset(mapping, 0, sizeof(*mapping));
          memcpy(mapping->name, map_name, strlen(map_name) + 1);
          mapping->index = (int32_t)gcu_array_count(&builder.map_mappings) - 1;
          mapped = mapping->index;
        }
        current_map = mapped;
      }
    }
    else if (gmdl_line_is(line_text, "mtllib", &rest)) {
      // The whole line is the path, spaces included - Blender exports
      // `mtllib my model.mtl` for a document saved under that name, and
      // taking the first token off it names a file that does not exist.
      // A bare `mtllib` does not clear the libraries already named:
      // measured 2026-09-23, Blender 4.3.2 reads the line as an unrecognized
      // element and still applies a material from a library an earlier line
      // named. It used to clear the compatibility field, which broke the
      // round trip; the steal at the end of this function says what happened
      // and why the field is derived now.
      GMDL_Result recorded = obj_record_path(
          rest, &builder.mtllibs, limits->max_mtllibs);
      if (recorded != GMDL_OK) {
        result = recorded;
        goto cleanup;
      }
    }
    else if (gmdl_line_is(line_text, "maplib", &rest)) {
      // Read exactly as `mtllib` is, whole line and all, and kept in a list
      // for the same reason. The documentation allows several paths on one
      // `maplib` line just as it does for `mtllib`; no reference settles
      // which reading is right, because no reference implements the
      // directive (3.15), so this follows the rule the library already has.
      GMDL_Result recorded = obj_record_path(
          rest, &builder.maplibs, limits->max_maplibs);
      if (recorded != GMDL_OK) {
        result = recorded;
        goto cleanup;
      }
    }
    else if (gmdl_line_is(line_text, "shadow_obj", &rest)) {
      // A path to another OBJ document, recorded and never opened (3.17).
      // The specification says one per file; this is a list because a
      // document carrying two would otherwise lose one without saying so.
      GMDL_Result recorded = obj_record_path(
          rest, &builder.shadow_objs, limits->max_shadow_objs);
      if (recorded != GMDL_OK) {
        result = recorded;
        goto cleanup;
      }
    }
    else if (gmdl_line_is(line_text, "trace_obj", &rest)) {
      GMDL_Result recorded = obj_record_path(
          rest, &builder.trace_objs, limits->max_trace_objs);
      if (recorded != GMDL_OK) {
        result = recorded;
        goto cleanup;
      }
    }
    // `ctech`, `stech` and `mg` are state for the free-form sub-language the
    // same way `cstype` and `deg` are, so they go onto the element rather
    // than into a list of their own (3.18). They were text until the model
    // they describe existed; it does now (3.19), and this is the breaking
    // change 3.18 documented as coming.
    else if (gmdl_line_is(line_text, "ctech", &rest)) {
      GMDL_Result parsed = obj_parse_ctech(
          rest, &current_freeform, limits->reject_non_finite);
      if (parsed != GMDL_OK) {
        result = parsed;
        goto cleanup;
      }
    }
    else if (gmdl_line_is(line_text, "stech", &rest)) {
      GMDL_Result parsed = obj_parse_stech(
          rest, &current_freeform, limits->reject_non_finite);
      if (parsed != GMDL_OK) {
        result = parsed;
        goto cleanup;
      }
    }
    else if (gmdl_line_is(line_text, "mg", &rest)) {
      GMDL_Result parsed = obj_parse_mg(
          rest, &current_freeform, limits->reject_non_finite);
      if (parsed != GMDL_OK) {
        result = parsed;
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

    obj->vertices = obj_steal_into(&builder.vertices, &obj->vertex_count);
    obj->colors = obj_steal_into(&builder.colors, &obj->color_count);
    obj->weights = obj_steal_into(&builder.weights, &obj->weight_count);
    obj->texcoords = obj_steal_into(&builder.texcoords, &obj->texcoord_count);
    obj->normals = obj_steal_into(&builder.normals, &obj->normal_count);
    obj->param_vertices =
        obj_steal_into(&builder.param_vertices, &obj->param_vertex_count);
    obj->freeforms = obj_steal_into(&builder.freeforms, &obj->freeform_count);
    obj->freeform_vertices = obj_steal_into(
        &builder.freeform_vertices, &obj->freeform_vertex_count);
    obj->basis_values =
        obj_steal_into(&builder.basis_values, &obj->basis_value_count);
    obj->freeform_bodies = obj_steal_into(
        &builder.freeform_bodies, &obj->freeform_body_count);
    obj->parm_values =
        obj_steal_into(&builder.parm_values, &obj->parm_value_count);
    obj->curve_refs =
        obj_steal_into(&builder.curve_refs, &obj->curve_ref_count);
    obj->special_points =
        obj_steal_into(&builder.special_points, &obj->special_point_count);
    obj->connections =
        obj_steal_into(&builder.connections, &obj->connection_count);
    obj->faces = obj_steal_into(&builder.faces, &obj->face_count);
    obj->lines = obj_steal_into(&builder.lines, &obj->line_count);
    obj->line_vertices = obj_steal_into(
        &builder.line_vertices, &obj->line_vertex_count);
    obj->points = obj_steal_into(&builder.points, &obj->point_count);
    obj->groups = obj_steal_into(&builder.groups, &obj->group_count);
    obj->material_mappings = obj_steal_into(
        &builder.material_mappings, &obj->material_mapping_count);
    obj->statements =
        obj_steal_into(&builder.statements, &obj->statement_count);
    obj->map_mappings =
        obj_steal_into(&builder.map_mappings, &obj->map_mapping_count);
    obj->render_states =
        obj_steal_into(&builder.render_states, &obj->render_state_count);
    obj->mtllibs = obj_steal_into(&builder.mtllibs, &obj->mtllib_count);
    obj->maplibs = obj_steal_into(&builder.maplibs, &obj->maplib_count);
    obj->shadow_objs =
        obj_steal_into(&builder.shadow_objs, &obj->shadow_obj_count);
    obj->trace_objs =
        obj_steal_into(&builder.trace_objs, &obj->trace_obj_count);

    // The compatibility field is derived from the list rather than
    // maintained alongside it, so the two cannot disagree. They did: a bare
    // `mtllib` after a real one cleared this field and left the list alone,
    // and since the dump writes the list, the reload came back with a field
    // the original did not have. The fuzzer found it against the corpus the
    // first time it was run after the list was added.
    if (obj->mtllib_count > 0) {
      memcpy(obj->mtllib, obj->mtllibs[0].path,
          strlen(obj->mtllibs[0].path) + 1);
    }
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

void gmdl_obj_options_default(GMDL_Obj_Options * options) {
  if (!options) {
    return;
  }
  // A line cap is the one limit that has to have a value: the parser reads a
  // line at a time, so without it a single unterminated line would be read
  // into memory in its entirety. The record caps are left open because the
  // size of the input already bounds them. A reading's zero is the behaviour
  // the specification states.
  *options = (GMDL_Obj_Options) {
    .max_line_length = GMDL_DEFAULT_MAX_LINE_LENGTH,
  };
}

void gmdl_obj_options_freecad(GMDL_Obj_Options * options) {
  if (!options) {
    return;
  }
  gmdl_obj_options_default(options);
  options->keep_byte_order_mark = true;
  options->keep_leading_whitespace = true;
  options->keep_inline_comments = true;
  options->no_line_continuation = true;
  options->omit_short_vertex = true;
  options->omit_non_finite_vertex = true;
  options->reject_extra_face_field = true;
  options->omit_malformed_faces = true;
  options->omit_unresolved_faces = true;
  options->omit_non_triangle_quad_faces = true;
  options->triangulate_quads = true;
  options->reject_unpaired_group_backslash = true;
}

void gmdl_obj_options_blender(GMDL_Obj_Options * options) {
  if (!options) {
    return;
  }
  gmdl_obj_options_default(options);
  options->accept_short_vertex = true;
  options->non_finite_becomes_zero = true;
  options->keep_byte_order_mark = true;
  options->omit_unresolved_faces = true;
  options->join_before_comment = true;
  options->group_line_is_one_name = true;
}

void gmdl_obj_options_vtk(GMDL_Obj_Options * options) {
  if (!options) {
    return;
  }
  gmdl_obj_options_default(options);
  options->reject_extra_face_field = true;
  options->non_finite_becomes_zero = true;
  options->reject_vertex_continuation = true;
  options->reject_face_comment = true;
  options->break_group_continuation = true;
  options->group_line_is_one_name = true;
  options->reject_short_texcoord = true;
}

/**
 * Read an OBJ document with the numeric locale pinned.
 *
 * The pin is here, around the whole parse, rather than at each of the
 * conversions inside it. See src/core/number_internal.h for why: the thing
 * worth making impossible is a future directive whose author does not know
 * this rule exists.
 */
GMDL_Result gmdl_obj_load(GMDL_Stream * stream, const GMDL_Obj_Options * limits,
    const GMDL_Allocator * allocator, GMDL_Obj ** out_obj) {
  GMDL_Numeric_Scope numeric;
  gmdl_numeric_scope_begin(&numeric);
  GMDL_Result result = obj_load_pinned(stream, limits, allocator, out_obj);
  gmdl_numeric_scope_end(&numeric);
  return result;
}

GMDL_Result gmdl_obj_load_file(const char * path, const GMDL_Obj_Options * limits,
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

  gcu_allocator_free(allocator, obj->mtllibs);
  gcu_allocator_free(allocator, obj->maplibs);
  gcu_allocator_free(allocator, obj->shadow_objs);
  gcu_allocator_free(allocator, obj->trace_objs);
  gcu_allocator_free(allocator, obj->vertices);
  gcu_allocator_free(allocator, obj->colors);
  gcu_allocator_free(allocator, obj->weights);
  gcu_allocator_free(allocator, obj->texcoords);
  gcu_allocator_free(allocator, obj->normals);
  gcu_allocator_free(allocator, obj->param_vertices);
  gcu_allocator_free(allocator, obj->freeforms);
  gcu_allocator_free(allocator, obj->freeform_vertices);
  gcu_allocator_free(allocator, obj->freeform_bodies);
  gcu_allocator_free(allocator, obj->parm_values);
  gcu_allocator_free(allocator, obj->curve_refs);
  gcu_allocator_free(allocator, obj->special_points);
  gcu_allocator_free(allocator, obj->connections);
  gcu_allocator_free(allocator, obj->basis_values);
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
  gcu_allocator_free(allocator, obj->map_mappings);
  gcu_allocator_free(allocator, obj->render_states);
  if (obj->statements) {
    for (size_t i = 0; i < obj->statement_count; i++) {
      gcu_allocator_free(allocator, obj->statements[i].text);
    }
    gcu_allocator_free(allocator, obj->statements);
  }
  gcu_allocator_free(allocator, obj);
}
