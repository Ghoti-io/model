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
 * Print a "usemtl" line when the material changes.
 *
 * Per https://paulbourke.net/dataformats/obj/ a material cannot be turned off,
 * only changed, and an unnamed material renders white.
 */
static int obj_dump_material(
    FILE * fd, const GMDL_Obj * obj, int32_t material_index) {
  if (material_index == -1) {
    return 0;
  }
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
 * Print a run of faces, emitting "usemtl" and "s" whenever they change.
 *
 * @param fd Destination.
 * @param obj The model.
 * @param start First face.
 * @param count How many.
 * @param smoothing The smoothing group currently in force, carried across
 *   calls and updated. A range cannot assume it starts at zero: the group
 *   before it may have left one set, and re-deriving it per range is how a
 *   dump that reads back with different smoothing gets written.
 * @return 0, or -1 on a write failure.
 */
static int obj_dump_face_range(FILE * fd, const GMDL_Obj * obj, size_t start,
    size_t count, int32_t * smoothing) {
  // -2 cannot be a material index, so the first face always prints its
  // material.
  int32_t last_material = -2;
  for (size_t i = start; i < start + count && i < obj->face_count; i++) {
    if (obj->faces[i].material_index != last_material) {
      if (obj_dump_material(fd, obj, obj->faces[i].material_index) < 0) {
        return -1;
      }
      last_material = obj->faces[i].material_index;
    }
    if (obj->faces[i].smoothing_group != *smoothing) {
      *smoothing = obj->faces[i].smoothing_group;
      // "off" rather than "0": both parse to zero, and it is the spelling
      // the format leads with.
      int written = *smoothing == 0 ? fprintf(fd, "s off\n")
                                    : fprintf(fd, "s %d\n", *smoothing);
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

/** Print the polylines and the points, which no group covers. */
static int obj_dump_lines_and_points(FILE * fd, const GMDL_Obj * obj) {
  for (size_t i = 0; i < obj->line_count; i++) {
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
    if (fprintf(fd, "p %d\n", obj->points[i] + 1) < 0) {
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

  // The parser starts with no smoothing group in force, so the dump does
  // too; a model whose faces all say zero therefore writes no "s" at all.
  int32_t smoothing = 0;

  if (obj->group_count > 0) {
    // Faces declared before the first "g" belong to no group (3.6). The
    // group loop below only walks group ranges, so without this they were
    // written nowhere and the dump did not round-trip - silently, because
    // the reload succeeded and simply had fewer faces.
    if (obj->groups[0].start_face > 0
        && obj_dump_face_range(
               fd, obj, 0, obj->groups[0].start_face, &smoothing)
            < 0) {
      return GMDL_ERR_IO;
    }
    for (size_t g = 0; g < obj->group_count; g++) {
      if (fprintf(fd, "g %s\n", obj->groups[g].name) < 0) {
        return GMDL_ERR_IO;
      }
      if (obj_dump_face_range(fd, obj, obj->groups[g].start_face,
              obj->groups[g].face_count, &smoothing)
          < 0) {
        return GMDL_ERR_IO;
      }
    }
  }
  else if (obj_dump_face_range(fd, obj, 0, obj->face_count, &smoothing) < 0) {
    return GMDL_ERR_IO;
  }

  if (obj_dump_lines_and_points(fd, obj) < 0) {
    return GMDL_ERR_IO;
  }

  return GMDL_OK;
}
