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
 * Wavefront MTL parsing.
 *
 * Reference document:
 *   http://www.paulbourke.net/dataformats/mtl/
 */

#include <ghoti.io/cutil/allocator.h>
#include <ghoti.io/cutil/array.h>
#include <stdio.h>
#include <string.h>

#include <ghoti.io/model/macros.h>
#include <ghoti.io/model/mtl.h>

#include "../obj/obj_internal.h"

GMDL_Result gmdl_mtl_load(GMDL_Stream * stream, const GMDL_Limits * limits,
    const GMDL_Allocator * allocator, GMDL_Mtl ** out_mtl) {
  if (!out_mtl) {
    return GMDL_ERR_INVALID;
  }
  *out_mtl = NULL;
  if (!stream) {
    return GMDL_ERR_INVALID;
  }

  GMDL_Limits defaults;
  if (!limits) {
    gmdl_limits_default(&defaults);
    limits = &defaults;
  }

  size_t line_size = limits->max_line_length ? limits->max_line_length : 65536;
  char * line = gcu_allocator_malloc(allocator, line_size + 1);
  if (!line) {
    return GMDL_ERR_OOM;
  }

  GCU_Array materials;
  if (!gcu_array_create_in_place(
          &materials, sizeof(GMDL_Mtl_Material), 8, allocator)) {
    gcu_allocator_free(allocator, line);
    return GMDL_ERR_OOM;
  }

  GMDL_Result result = GMDL_OK;
  // The material being filled in. Held as an index rather than a pointer,
  // because appending another material may move the array.
  size_t current = 0;
  bool have_current = false;

  GMDL_Line_Reader reader;
  gmdl_line_reader_init(&reader, stream, line, line_size);

  for (;;) {
    const char * line_text = NULL;
    GMDL_Result line_result = gmdl_line_next(&reader, &line_text);
    if (line_result == GMDL_ERR_IO) {
      break; // End of stream.
    }
    if (line_result != GMDL_OK) {
      result = line_result;
      goto cleanup;
    }

    // A blank line, or one that was wholly a comment, has nothing to do.
    if (line_text[0] == '\0') {
      continue;
    }

    const char * rest = NULL;
    if (gmdl_line_is(line_text, "newmtl", &rest)) {
      char name[GMDL_MTL_MAX_NAME_LENGTH];
      if (sscanf(rest, "%127s", name) != 1) {
        result = GMDL_ERR_FORMAT;
        goto cleanup;
      }
      if (gmdl_limit_reached(
              gcu_array_count(&materials), limits->max_materials)) {
        result = GMDL_ERR_LIMIT;
        goto cleanup;
      }
      GMDL_Mtl_Material * material =
          (GMDL_Mtl_Material *)gcu_array_emplace(&materials);
      if (!material) {
        result = GMDL_ERR_OOM;
        goto cleanup;
      }
      memset(material, 0, sizeof(*material));
      memcpy(material->name, name, strlen(name) + 1);
      current = gcu_array_count(&materials) - 1;
      have_current = true;
      continue;
    }

    if (!have_current) {
      // A property before any "newmtl" has nothing to apply to.
      continue;
    }

    GMDL_Mtl_Material * material =
        (GMDL_Mtl_Material *)gcu_array_at(&materials, current);

    if (gmdl_line_is(line_text, "Ka", &rest)) {
      if (sscanf(rest, "%f %f %f", &material->Ka[0], &material->Ka[1],
              &material->Ka[2]) != 3) {
        result = GMDL_ERR_FORMAT;
        goto cleanup;
      }
    }
    else if (gmdl_line_is(line_text, "Kd", &rest)) {
      if (sscanf(rest, "%f %f %f", &material->Kd[0], &material->Kd[1],
              &material->Kd[2]) != 3) {
        result = GMDL_ERR_FORMAT;
        goto cleanup;
      }
    }
    else if (gmdl_line_is(line_text, "Ks", &rest)) {
      if (sscanf(rest, "%f %f %f", &material->Ks[0], &material->Ks[1],
              &material->Ks[2]) != 3) {
        result = GMDL_ERR_FORMAT;
        goto cleanup;
      }
    }
    else if (gmdl_line_is(line_text, "Ns", &rest)) {
      if (sscanf(rest, "%f", &material->Ns) != 1) {
        result = GMDL_ERR_FORMAT;
        goto cleanup;
      }
    }
    else if (gmdl_line_is(line_text, "d", &rest)) {
      if (sscanf(rest, "%f", &material->d) != 1) {
        result = GMDL_ERR_FORMAT;
        goto cleanup;
      }
    }
    else if (gmdl_line_is(line_text, "illum", &rest)) {
      int value = 0;
      if (sscanf(rest, "%d", &value) != 1) {
        result = GMDL_ERR_FORMAT;
        goto cleanup;
      }
      material->illum = (int32_t)value;
    }
    // Everything else - map_Kd, Ni, Tr, and the rest - is ignored.
  }

  {
    GMDL_Mtl * mtl = gcu_allocator_calloc(allocator, 1, sizeof(GMDL_Mtl));
    if (!mtl) {
      result = GMDL_ERR_OOM;
      goto cleanup;
    }
    (void)gcu_array_shrink_to_fit(&materials);
    size_t count = 0;
    mtl->materials = (GMDL_Mtl_Material *)gcu_array_steal(&materials, &count);
    mtl->material_count = count;
    mtl->allocator = allocator;

    gcu_array_destroy_in_place(&materials);
    gcu_allocator_free(allocator, line);
    *out_mtl = mtl;
    return GMDL_OK;
  }

cleanup:
  gcu_array_destroy_in_place(&materials);
  gcu_allocator_free(allocator, line);
  return result;
}

GMDL_Result gmdl_mtl_load_file(const char * path, const GMDL_Limits * limits,
    const GMDL_Allocator * allocator, GMDL_Mtl ** out_mtl) {
  if (!out_mtl) {
    return GMDL_ERR_INVALID;
  }
  *out_mtl = NULL;

  GMDL_Stream * stream = NULL;
  GMDL_Result result = gmdl_stream_create_file(path, allocator, &stream);
  if (result != GMDL_OK) {
    return result;
  }

  result = gmdl_mtl_load(stream, limits, allocator, out_mtl);
  gmdl_stream_destroy(stream);
  return result;
}

void gmdl_mtl_free(GMDL_Mtl * mtl) {
  if (!mtl) {
    return;
  }
  const GMDL_Allocator * allocator = mtl->allocator;
  gcu_allocator_free(allocator, mtl->materials);
  gcu_allocator_free(allocator, mtl);
}

const GMDL_Mtl_Material * gmdl_mtl_find(
    const GMDL_Mtl * mtl, const char * name) {
  if (!mtl || !name || !mtl->materials) {
    return NULL;
  }
  for (size_t i = 0; i < mtl->material_count; i++) {
    if (strcmp(mtl->materials[i].name, name) == 0) {
      return &mtl->materials[i];
    }
  }
  return NULL;
}
