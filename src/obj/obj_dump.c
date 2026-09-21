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

/** Print a run of faces, emitting "usemtl" whenever the material changes. */
static int obj_dump_face_range(
    FILE * fd, const GMDL_Obj * obj, size_t start, size_t count) {
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
    if (obj_dump_face(fd, &obj->faces[i]) < 0) {
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
    if (fprintf(fd, "v %f %f %f\n", obj->vertices[i].x, obj->vertices[i].y,
            obj->vertices[i].z) < 0) {
      return GMDL_ERR_IO;
    }
  }
  for (size_t i = 0; i < obj->texcoord_count; i++) {
    if (fprintf(fd, "vt %f %f\n", obj->texcoords[i].u, obj->texcoords[i].v)
        < 0) {
      return GMDL_ERR_IO;
    }
  }
  for (size_t i = 0; i < obj->normal_count; i++) {
    if (fprintf(fd, "vn %f %f %f\n", obj->normals[i].x, obj->normals[i].y,
            obj->normals[i].z) < 0) {
      return GMDL_ERR_IO;
    }
  }

  if (obj->group_count > 0) {
    for (size_t g = 0; g < obj->group_count; g++) {
      if (fprintf(fd, "g %s\n", obj->groups[g].name) < 0) {
        return GMDL_ERR_IO;
      }
      if (obj_dump_face_range(
              fd, obj, obj->groups[g].start_face, obj->groups[g].face_count)
          < 0) {
        return GMDL_ERR_IO;
      }
    }
  }
  else if (obj_dump_face_range(fd, obj, 0, obj->face_count) < 0) {
    return GMDL_ERR_IO;
  }

  return GMDL_OK;
}
