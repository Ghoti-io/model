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
 * Writing a parsed model back out as OBJ text.
 */

#include <stdbool.h>
#include <stdio.h>

#include <ghoti.io/model/macros.h>
#include <ghoti.io/model/obj.h>

/** Print one "v", "v/vt", "v//vn" or "v/vt/vn" reference. */
static int obj_dump_reference(
    FILE * fd, int32_t vertex, int32_t texcoord, int32_t normal) {
  if (fprintf(fd, " %d", vertex + 1) < 0) {
    return -1;
  }
  if (texcoord == -1 && normal == -1) {
    return 0;
  }
  if (fprintf(fd, "/") < 0) {
    return -1;
  }
  if (texcoord != -1 && fprintf(fd, "%d", texcoord + 1) < 0) {
    return -1;
  }
  if (normal != -1 && fprintf(fd, "/%d", normal + 1) < 0) {
    return -1;
  }
  return 0;
}

/** Print one face, including any vertices held in its overflow array. */
static int obj_dump_face(FILE * fd, const GMDL_Obj_Face * face) {
  if (fprintf(fd, "f") < 0) {
    return -1;
  }

  size_t inline_count = face->count > 4 ? 4 : face->count;
  for (size_t j = 0; j < inline_count; j++) {
    if (obj_dump_reference(
            fd, face->vertex[j], face->texcoord[j], face->normal[j]) < 0) {
      return -1;
    }
  }

  if (face->count > 4 && face->overflow) {
    size_t overflow_count = face->count - 4;
    for (size_t j = 0; j < overflow_count; j++) {
      if (obj_dump_reference(fd, face->overflow[j].vertex,
              face->overflow[j].texcoord, face->overflow[j].normal) < 0) {
        return -1;
      }
    }
  }

  return fprintf(fd, "\n") < 0 ? -1 : 0;
}

/**
 * The state the dump carries from one element to the next.
 *
 * `usemtl` and `s` are both state in the file, and the dump emits elements in
 * several runs - the faces before the first `g`, then each group, then the
 * polylines, then the points. The two need carrying for different reasons,
 * and only one of them is a correctness question.
 *
 * **Smoothing must be carried.** Zero is a real value and also the state a
 * file starts in, so a run that begins afresh writes nothing for a face at
 * zero - and if the run before it left `s 7` in force, the reload smooths
 * faces the source did not. Measured: a mutation that re-derives it per run
 * fails ObjSmoothing.SurvivesAcrossGroupBoundaries.
 *
 * **Material is carried for the output's sake.** Beginning afresh is still
 * *correct*, because -1 means "none" and is never written either way, so any
 * real material differs from the fresh state and gets its `usemtl`. What it
 * produces is a redundant line at every run boundary. Carrying it means the
 * file says what the model says and no more.
 */
typedef struct {
  int32_t material;  ///< Material index the file currently names.
  int32_t smoothing; ///< Smoothing group currently in force.
} obj_dump_state_t;

/**
 * Print a "usemtl" line naming a material.
 *
 * Per https://paulbourke.net/dataformats/obj/ an unnamed material renders
 * white, which is what a `material_index` naming no mapping becomes.
 */
static int obj_dump_material(
    FILE * fd, const GMDL_Obj * obj, int32_t material_index) {
  for (size_t j = 0; j < obj->material_mapping_count; j++) {
    if (obj->material_mappings[j].index == material_index) {
      return fprintf(fd, "usemtl %s\n", obj->material_mappings[j].name) < 0
          ? -1
          : 0;
    }
  }
  return fprintf(fd, "usemtl white\n") < 0 ? -1 : 0;
}

/**
 * Move the file's material to @p wanted, writing a `usemtl` if that changes
 * it.
 *
 * A material cannot be turned off, only changed, so an element that names
 * none while one is in force has no OBJ spelling: nothing is written and the
 * reload reads the previous material. Section 9 records that; it is not a
 * state a parse produces, since `material_index` only ever moves from -1 to a
 * mapping and never back.
 *
 * @param fd Destination.
 * @param obj The model.
 * @param wanted The material the element names.
 * @param state Carried state, updated.
 * @return 0, or -1 on a write failure.
 */
static int obj_dump_material_change(
    FILE * fd, const GMDL_Obj * obj, int32_t wanted, obj_dump_state_t * state) {
  if (wanted == state->material) {
    return 0;
  }
  state->material = wanted;
  if (wanted == -1) {
    return 0;
  }
  return obj_dump_material(fd, obj, wanted);
}

/**
 * Print a run of faces, emitting "usemtl" and "s" whenever they change.
 *
 * @param fd Destination.
 * @param obj The model.
 * @param start First face.
 * @param count How many.
 * @param state The material and smoothing group the file currently names,
 *   carried across calls and updated. A range cannot assume either starts
 *   fresh: the run before it may have left one set, and re-deriving them per
 *   range is how a dump that reads back differently gets written.
 * @return 0, or -1 on a write failure.
 */
static int obj_dump_face_range(FILE * fd, const GMDL_Obj * obj, size_t start,
    size_t count, obj_dump_state_t * state) {
  for (size_t i = start; i < start + count && i < obj->face_count; i++) {
    if (obj_dump_material_change(fd, obj, obj->faces[i].material_index, state)
        < 0) {
      return -1;
    }
    if (obj->faces[i].smoothing_group != state->smoothing) {
      state->smoothing = obj->faces[i].smoothing_group;
      // "off" rather than "0": both parse to zero, and it is the spelling
      // the format leads with.
      int written = state->smoothing == 0
          ? fprintf(fd, "s off\n")
          : fprintf(fd, "s %d\n", state->smoothing);
      if (written < 0) {
        return -1;
      }
    }
    if (obj_dump_face(fd, &obj->faces[i]) < 0) {
      return -1;
    }
  }
  return 0;
}

/**
 * Print the polylines and the points, which no group covers.
 *
 * Called twice, because OBJ cannot turn a material off. An element declared
 * before the file's first `usemtl` carries -1, and once anything has set a
 * material there is no way to write that element and have it read back the
 * same. The elements holding -1 therefore go out **before** the faces, while
 * nothing is in force yet, and the rest after.
 *
 * That split preserves the arrays' own order rather than disturbing it:
 * within one parse `material_index` moves from -1 to a mapping and never
 * back, so the -1 entries are a prefix of each array and the two passes
 * emit the prefix and then the remainder.
 *
 * Found by the fuzzer within ninety seconds of the material being added to
 * these elements: "l 1 2" before any usemtl, then a usemtl and a face, and
 * the polyline came back carrying the face's material.
 *
 * @param fd Destination.
 * @param obj The model.
 * @param state Carried material and smoothing state.
 * @param unmaterialed true for the elements naming no material, false for
 *   the rest.
 * @return 0, or -1 on a write failure.
 */
static int obj_dump_lines_and_points(FILE * fd, const GMDL_Obj * obj,
    obj_dump_state_t * state, bool unmaterialed) {
  for (size_t i = 0; i < obj->line_count; i++) {
    if ((obj->lines[i].material_index == -1) != unmaterialed) {
      continue;
    }
    if (obj_dump_material_change(fd, obj, obj->lines[i].material_index, state)
        < 0) {
      return -1;
    }
    if (fprintf(fd, "l") < 0) {
      return -1;
    }
    const GMDL_Obj_Line * element = &obj->lines[i];
    for (size_t j = 0; j < element->count; j++) {
      const GMDL_Obj_Line_Vertex * entry =
          &obj->line_vertices[element->start + j];
      int written = entry->texcoord == -1
          ? fprintf(fd, " %d", entry->vertex + 1)
          : fprintf(fd, " %d/%d", entry->vertex + 1, entry->texcoord + 1);
      if (written < 0) {
        return -1;
      }
    }
    if (fprintf(fd, "\n") < 0) {
      return -1;
    }
  }

  // One statement per point. The format allows several on a line and the
  // grouping means nothing once parsed, so there is nothing to preserve.
  for (size_t i = 0; i < obj->point_count; i++) {
    if ((obj->points[i].material_index == -1) != unmaterialed) {
      continue;
    }
    if (obj_dump_material_change(fd, obj, obj->points[i].material_index, state)
        < 0) {
      return -1;
    }
    if (fprintf(fd, "p %d\n", obj->points[i].vertex + 1) < 0) {
      return -1;
    }
  }
  return 0;
}

GMDL_Result gmdl_obj_dump(const GMDL_Obj * obj, FILE * fd) {
  if (!obj || !fd) {
    return GMDL_ERR_INVALID;
  }

  if (obj->mtllib[0] != '\0'
      && fprintf(fd, "mtllib %s\n", obj->mtllib) < 0) {
    return GMDL_ERR_IO;
  }

  for (size_t i = 0; i < obj->vertex_count; i++) {
    if (fprintf(fd, "v %.9g %.9g %.9g\n", obj->vertices[i].x, obj->vertices[i].y,
            obj->vertices[i].z) < 0) {
      return GMDL_ERR_IO;
    }
  }
  for (size_t i = 0; i < obj->texcoord_count; i++) {
    if (fprintf(fd, "vt %.9g %.9g\n", obj->texcoords[i].u, obj->texcoords[i].v)
        < 0) {
      return GMDL_ERR_IO;
    }
  }
  for (size_t i = 0; i < obj->normal_count; i++) {
    if (fprintf(fd, "vn %.9g %.9g %.9g\n", obj->normals[i].x, obj->normals[i].y,
            obj->normals[i].z) < 0) {
      return GMDL_ERR_IO;
    }
  }

  // The parser starts with no material and no smoothing group in force, so
  // the dump does too; a model whose faces all say zero writes no "s" at all.
  obj_dump_state_t state = {-1, 0};

  // The general statements lead, in file order. Their position relative to
  // the geometry is not recorded - nothing else in this model is ordered
  // against the geometry either - and they are text this library never acts
  // on, so writing them first costs nothing a consumer can observe.
  for (size_t i = 0; i < obj->statement_count; i++) {
    const char * directive =
        obj->statements[i].kind == GMDL_OBJ_STATEMENT_CALL ? "call" : "csh";
    if (fprintf(fd, "%s %s\n", directive, obj->statements[i].text) < 0) {
      return GMDL_ERR_IO;
    }
  }

  // Everything naming no material first, while none is in force. See
  // obj_dump_lines_and_points().
  if (obj_dump_lines_and_points(fd, obj, &state, true) < 0) {
    return GMDL_ERR_IO;
  }

  if (obj->group_count > 0) {
    // Faces declared before the first "g" belong to no group (3.6). The
    // group loop below only walks group ranges, so without this they were
    // written nowhere and the dump did not round-trip - silently, because
    // the reload succeeded and simply had fewer faces.
    if (obj->groups[0].start_face > 0
        && obj_dump_face_range(
               fd, obj, 0, obj->groups[0].start_face, &state)
            < 0) {
      return GMDL_ERR_IO;
    }
    for (size_t g = 0; g < obj->group_count; g++) {
      if (fprintf(fd, "g %s\n", obj->groups[g].name) < 0) {
        return GMDL_ERR_IO;
      }
      if (obj_dump_face_range(fd, obj, obj->groups[g].start_face,
              obj->groups[g].face_count, &state)
          < 0) {
        return GMDL_ERR_IO;
      }
    }
  }
  else if (obj_dump_face_range(fd, obj, 0, obj->face_count, &state) < 0) {
    return GMDL_ERR_IO;
  }

  if (obj_dump_lines_and_points(fd, obj, &state, false) < 0) {
    return GMDL_ERR_IO;
  }

  return GMDL_OK;
}
