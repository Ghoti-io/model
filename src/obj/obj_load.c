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
  GCU_Array texcoords;
  GCU_Array normals;
  GCU_Array faces;
  GCU_Array lines;
  GCU_Array line_vertices;
  GCU_Array points;
  GCU_Array groups;
  GCU_Array material_mappings;
  const GMDL_Allocator * allocator;
} obj_builder_t;

static bool obj_builder_init(
    obj_builder_t * b, const GMDL_Allocator * allocator) {
  memset(b, 0, sizeof(*b));
  b->allocator = allocator;
  return gcu_array_create_in_place(
             &b->vertices, sizeof(GMDL_Obj_Vertex), 128, allocator)
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
          sizeof(GMDL_Obj_Material_Mapping), 4, allocator);
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
  gcu_array_destroy_in_place(&b->vertices);
  gcu_array_destroy_in_place(&b->texcoords);
  gcu_array_destroy_in_place(&b->normals);
  gcu_array_destroy_in_place(&b->faces);
  gcu_array_destroy_in_place(&b->lines);
  gcu_array_destroy_in_place(&b->line_vertices);
  gcu_array_destroy_in_place(&b->points);
  gcu_array_destroy_in_place(&b->groups);
  gcu_array_destroy_in_place(&b->material_mappings);
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
  if (value < 0) {
    // Relative: -1 is the last one declared.
    return (int32_t)((long)declared + value);
  }
  return (int32_t)(value - 1);
}

GMDL_Result gmdl_obj_load(GMDL_Stream * stream, const GMDL_Limits * limits,
    const GMDL_Allocator * allocator, GMDL_Obj ** out_obj) {
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
      GMDL_Obj_Vertex v;
      if (sscanf(rest, "%f %f %f", &v.x, &v.y, &v.z) != 3) {
        result = GMDL_ERR_FORMAT;
        goto cleanup;
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
    }
    else if (gmdl_line_is(line_text, "vt", &rest)) {
      GMDL_Obj_TexCoord vt;
      if (sscanf(rest, "%f %f", &vt.u, &vt.v) != 2) {
        result = GMDL_ERR_FORMAT;
        goto cleanup;
      }
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
      GMDL_Obj_Normal vn;
      if (sscanf(rest, "%f %f %f", &vn.x, &vn.y, &vn.z) != 3) {
        result = GMDL_ERR_FORMAT;
        goto cleanup;
      }
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
              gcu_array_count(&builder.faces), limits->max_faces)) {
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
      // "g" with no name means the default group, per the specification; a
      // name too long for the field is refused rather than cut, because the
      // first 127 bytes of a name name something else (3.9).
      char name[GMDL_OBJ_MAX_NAME_LENGTH];
      GMDL_Result named = gmdl_first_token(rest, name, sizeof(name));
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
      current_group = (long)gcu_array_count(&builder.groups) - 1;
    }
    else if (gmdl_line_is(line_text, "l", &rest)) {
      if (gmdl_limit_reached(
              gcu_array_count(&builder.lines), limits->max_faces)) {
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
                gcu_array_count(&builder.points), limits->max_faces)) {
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
        int value = 0;
        if (sscanf(rest, "%d", &value) != 1) {
          result = GMDL_ERR_FORMAT;
          goto cleanup;
        }
        current_smoothing = (int32_t)value;
      }
    }
    else if (gmdl_line_is(line_text, "usemtl", &rest)) {
      // A bare "usemtl" is GMDL_ERR_FORMAT (3.7); an over-long one is
      // GMDL_ERR_LIMIT (3.9). gmdl_first_token() distinguishes them.
      char mtl_name[GMDL_OBJ_MAX_NAME_LENGTH];
      GMDL_Result named = gmdl_first_token(rest, mtl_name, sizeof(mtl_name));
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
      // A bare "mtllib" clears the path (3.8); one too long for the field is
      // GMDL_ERR_LIMIT (3.9), since a path cut at 255 bytes names a
      // different file, or none.
      GMDL_Result named = gmdl_first_token(rest, mtllib, sizeof(mtllib));
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
  gcu_allocator_free(allocator, obj);
}
