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
 * Geomview Object File Format (OFF) parsing.
 *
 * There is no standards-body specification. The behaviours this library
 * records are in documentation/off.md.
 */

#ifndef GHOTI_IO_GMDL_OFF_H
#define GHOTI_IO_GMDL_OFF_H

#include <ghoti.io/model/core.h>
#include <ghoti.io/model/macros.h>
#include <ghoti.io/model/stream.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Which optional fields a vertex stated.
 */
typedef enum GMDL_Off_Vertex_Present {
  GMDL_OFF_VERTEX_HAS_NORMAL = 1u << 0, ///< `nx ny nz` after the position.
  GMDL_OFF_VERTEX_HAS_COLOR = 1u << 1,  ///< `r g b` or `r g b a` after that.
} GMDL_Off_Vertex_Present;

/**
 * @brief Which optional fields a face stated.
 */
typedef enum GMDL_Off_Face_Present {
  GMDL_OFF_FACE_HAS_COLOR = 1u << 0,       ///< RGB[A] after the indices.
  GMDL_OFF_FACE_HAS_COLOR_INDEX = 1u << 1, ///< One integer colormap index.
} GMDL_Off_Face_Present;

/**
 * @brief Which optional fields the document stated.
 */
typedef enum GMDL_Off_Present {
  GMDL_OFF_HAS_VERTEX_NORMALS = 1u << 0, ///< Keyword carried `N` (`NOFF`/`CNOFF`).
  GMDL_OFF_HAS_VERTEX_COLORS = 1u << 1,  ///< Keyword carried `C` (`COFF`/`CNOFF`).
} GMDL_Off_Present;

/**
 * @brief One vertex.
 *
 * Position is always present. Normal and colour follow the keyword prefixes
 * in documentation/off.md.
 */
typedef struct GMDL_Off_Vertex {
  float position[3]; ///< `x y z`.
  float normal[3];   ///< When ::GMDL_OFF_VERTEX_HAS_NORMAL.
  float r;           ///< Red in [0, 1] when colour is present.
  float g;           ///< Green in [0, 1] when colour is present.
  float b;           ///< Blue in [0, 1] when colour is present.
  float a;           ///< Alpha in [0, 1]; 1 when the file gave only RGB.
  uint32_t present;  ///< ::GMDL_Off_Vertex_Present bits.
} GMDL_Off_Vertex;

/**
 * @brief One face.
 *
 * `indices` holds `index_count` zero-based vertex indices. The array is owned
 * by the parent ::GMDL_Off and freed with it.
 */
typedef struct GMDL_Off_Face {
  uint32_t * indices; ///< Corner indices into `vertices`.
  size_t index_count; ///< Number of corners; at least 3 for a polygon.
  float r;            ///< Red in [0, 1] when ::GMDL_OFF_FACE_HAS_COLOR.
  float g;            ///< Green in [0, 1] when ::GMDL_OFF_FACE_HAS_COLOR.
  float b;            ///< Blue in [0, 1] when ::GMDL_OFF_FACE_HAS_COLOR.
  float a;            ///< Alpha in [0, 1]; 1 when the file gave only RGB.
  int32_t color_index; ///< When ::GMDL_OFF_FACE_HAS_COLOR_INDEX.
  uint32_t present;   ///< ::GMDL_Off_Face_Present bits.
} GMDL_Off_Face;

/**
 * @brief A parsed OFF document.
 */
typedef struct GMDL_Off {
  uint32_t present; ///< ::GMDL_Off_Present bits from the keyword.
  size_t edge_count; ///< Third count from the header; not checked on load.
  GMDL_Off_Vertex * vertices; ///< Or NULL when there are none.
  size_t vertex_count; ///< Number of entries in vertices.
  GMDL_Off_Face * faces; ///< Or NULL when there are none.
  size_t face_count; ///< Number of entries in faces.
  const GMDL_Allocator * allocator; ///< Owns vertices, faces, and index arrays.
} GMDL_Off;

/**
 * @brief Caps and readings for one OFF parse or dump.
 *
 * Caps come first. Zero for a size_t cap means no cap, except
 * `max_line_length`, where zero means ::GMDL_DEFAULT_MAX_LINE_LENGTH.
 *
 * A reading's zero is the behaviour documentation/off.md states. Pass NULL
 * to a load or dump for gmdl_off_options_default().
 */
typedef struct GMDL_Off_Options {
  /**
   * Longest accepted line, in bytes, not counting its ending.
   *
   * Zero means ::GMDL_DEFAULT_MAX_LINE_LENGTH, not "unlimited".
   */
  size_t max_line_length;
  size_t max_vertices;     ///< Cap on vertices.
  size_t max_faces;        ///< Cap on faces.
  size_t max_face_corners; ///< Cap on corners of one face.

  /**
   * Reject `nan` and `inf` on positions, normals and colours.
   *
   * Zero, the default, records the value.
   */
  bool reject_non_finite;
} GMDL_Off_Options;

/**
 * @brief Fill in the default OFF options.
 *
 * @param options Structure to populate. NULL is ignored.
 */
GMDL_API void gmdl_off_options_default(GMDL_Off_Options * options);

/**
 * @brief Parse an OFF file from a stream.
 *
 * @param stream Stream positioned at the start of the OFF data.
 * @param options Caps and readings, or NULL for gmdl_off_options_default().
 * @param allocator Allocator for the result, or NULL for the default.
 * @param out_off Receives the parsed model on success.
 * @return GMDL_OK, or an error code.
 */
GMDL_API GMDL_Result gmdl_off_load(GMDL_Stream * stream,
    const GMDL_Off_Options * options, const GMDL_Allocator * allocator,
    GMDL_Off ** out_off);

/**
 * @brief Parse an OFF file from a path.
 *
 * @param path Path to the OFF file.
 * @param options Caps and readings, or NULL for gmdl_off_options_default().
 * @param allocator Allocator for the result, or NULL for the default.
 * @param out_off Receives the parsed model on success.
 * @return GMDL_OK, or an error code.
 */
GMDL_API GMDL_Result gmdl_off_load_file(const char * path,
    const GMDL_Off_Options * options, const GMDL_Allocator * allocator,
    GMDL_Off ** out_off);

/**
 * @brief Free a model. NULL is ignored.
 *
 * @param off The model.
 */
GMDL_API void gmdl_off_free(GMDL_Off * off);

/**
 * @brief Write a model as ASCII OFF.
 *
 * Always ASCII. The keyword is `OFF`, `COFF`, `NOFF` or `CNOFF` from what the
 * model carries. Counts are written on the line after the keyword so FreeCAD
 * and OpenSCAD both reload the dump.
 *
 * @param off The model.
 * @param options Caps and readings, or NULL for defaults.
 * @param fd Destination.
 * @return GMDL_OK, GMDL_ERR_INVALID, or GMDL_ERR_IO.
 */
GMDL_API GMDL_Result gmdl_off_dump(const GMDL_Off * off,
    const GMDL_Off_Options * options, FILE * fd);

#ifdef __cplusplus
}
#endif

#endif // GHOTI_IO_GMDL_OFF_H
