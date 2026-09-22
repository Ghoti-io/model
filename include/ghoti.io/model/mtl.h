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
 * Wavefront MTL material parsing.
 *
 * Reference document:
 *   http://www.paulbourke.net/dataformats/mtl/
 */

#ifndef GHOTI_IO_GMDL_MTL_H
#define GHOTI_IO_GMDL_MTL_H

#include <ghoti.io/model/core.h>
#include <ghoti.io/model/macros.h>
#include <ghoti.io/model/stream.h>
#include <stdint.h>
#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Longest material name retained, including the NUL. */
#define GMDL_MTL_MAX_NAME_LENGTH 128

/**
 * @brief Which properties a material's source actually stated.
 *
 * Every field of ::GMDL_Mtl_Material holds a value whether or not the file
 * said anything, so the value alone cannot distinguish "black" from "not
 * mentioned". For most properties that does not matter, because readers
 * treat the two alike. For `Kd` it matters a great deal: an absent one is a
 * light default - white in VTK, grey in Blender - and `Kd 0 0 0` is black.
 *
 * ::gmdl_mtl_dump() writes only the properties whose bit is set, so a
 * material this library read and wrote back says exactly what it was given
 * and no more. A consumer building a ::GMDL_Mtl_Material by hand sets the
 * bits for the properties it filled in; leaving `present` at 0 means "this
 * material states nothing", which dumps as a bare `newmtl`.
 */
typedef enum GMDL_Mtl_Present {
  GMDL_MTL_HAS_KA = 1u << 0,    ///< `Ka` was stated.
  GMDL_MTL_HAS_KD = 1u << 1,    ///< `Kd` was stated.
  GMDL_MTL_HAS_KS = 1u << 2,    ///< `Ks` was stated.
  GMDL_MTL_HAS_NS = 1u << 3,    ///< `Ns` was stated.
  GMDL_MTL_HAS_D = 1u << 4,     ///< `d` was stated.
  GMDL_MTL_HAS_ILLUM = 1u << 5, ///< `illum` was stated.
} GMDL_Mtl_Present;

/**
 * @brief A single material definition.
 *
 * Each property field holds a usable value whatever the file said; `present`
 * says which of them the file actually stated. A renderer can ignore
 * `present` and get sensible material; a writer must not.
 */
typedef struct {
  char name[GMDL_MTL_MAX_NAME_LENGTH]; ///< Material name.
  float Ka[3]; ///< Ambient colour (RGB).
  float Kd[3]; ///< Diffuse colour (RGB).
  float Ks[3]; ///< Specular colour (RGB).
  float Ns;    ///< Specular exponent.
  float d;     ///< Dissolve (opacity; 1.0 is opaque, and the default).
  int32_t illum; ///< Illumination model.
  uint32_t present; ///< Bitwise OR of ::GMDL_Mtl_Present.
} GMDL_Mtl_Material;

/**
 * @brief A parsed MTL file.
 */
typedef struct {
  GMDL_Mtl_Material * materials; ///< Materials, or NULL when there are none.
  size_t material_count;         ///< Number of materials.
  const GMDL_Allocator * allocator; ///< Allocator that owns `materials`.
} GMDL_Mtl;

/**
 * @brief Parse an MTL file from a stream.
 *
 * @param stream Stream positioned at the start of the MTL data.
 * @param limits Parsing caps, or NULL for gmdl_limits_default().
 * @param allocator Allocator for the result, or NULL for the default.
 * @param out_mtl Receives the parsed material library on success.
 * @return GMDL_OK, or GMDL_ERR_FORMAT, GMDL_ERR_LIMIT, GMDL_ERR_OOM, or
 *   GMDL_ERR_INVALID.
 */
GMDL_API GMDL_Result gmdl_mtl_load(GMDL_Stream * stream,
    const GMDL_Limits * limits, const GMDL_Allocator * allocator,
    GMDL_Mtl ** out_mtl);

/**
 * @brief Parse an MTL file from a path.
 *
 * @param path Path to the MTL file.
 * @param limits Parsing caps, or NULL for gmdl_limits_default().
 * @param allocator Allocator for the result, or NULL for the default.
 * @param out_mtl Receives the parsed material library on success.
 * @return GMDL_OK, GMDL_ERR_IO if the file cannot be read, or any result
 *   gmdl_mtl_load() can return.
 */
GMDL_API GMDL_Result gmdl_mtl_load_file(const char * path,
    const GMDL_Limits * limits, const GMDL_Allocator * allocator,
    GMDL_Mtl ** out_mtl);

/**
 * @brief Free a material library returned by gmdl_mtl_load(). NULL is ignored.
 *
 * @param mtl The material library.
 */
GMDL_API void gmdl_mtl_free(GMDL_Mtl * mtl);

/**
 * @brief Look up a material by name.
 *
 * @param mtl The material library.
 * @param name Material name to find.
 * @return The material, or NULL if there is no such material.
 */
GMDL_API const GMDL_Mtl_Material * gmdl_mtl_find(
    const GMDL_Mtl * mtl, const char * name);

/**
 * @brief Write a material library back out as MTL text.
 *
 * @param mtl The material library.
 * @param fd Destination, e.g. stdout.
 * @return GMDL_OK, GMDL_ERR_INVALID, or GMDL_ERR_IO.
 */
GMDL_API GMDL_Result gmdl_mtl_dump(const GMDL_Mtl * mtl, FILE * fd);

#ifdef __cplusplus
}
#endif

#endif // GHOTI_IO_GMDL_MTL_H
