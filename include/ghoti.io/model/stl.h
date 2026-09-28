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
 * STL triangle mesh parsing.
 *
 * There is no standards-body specification. The behaviours this library
 * records are in documentation/stl.md.
 */

#ifndef GHOTI_IO_GMDL_STL_H
#define GHOTI_IO_GMDL_STL_H

#include <ghoti.io/model/core.h>
#include <ghoti.io/model/macros.h>
#include <ghoti.io/model/stream.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Longest solid name retained, including the NUL. */
#define GMDL_STL_MAX_NAME_LENGTH 128

/** Bytes in a binary STL header. */
#define GMDL_STL_HEADER_SIZE 80

/**
 * @brief Which encoding a loaded document used.
 *
 * The dump may write the other form when ::GMDL_Stl_Options.write_ascii is
 * set. This field is what the file used, not what the dump will use.
 */
typedef enum GMDL_Stl_Form {
  GMDL_STL_FORM_ASCII = 0,  ///< `solid` / `facet` / `endsolid` text.
  GMDL_STL_FORM_BINARY,     ///< 80-byte header, count, 50-byte facets.
} GMDL_Stl_Form;

/**
 * @brief How the per-triangle `uint16` attribute is read as a colour.
 *
 * Zero, ::GMDL_STL_COLOR_NONE, records the attribute and does not invent a
 * colour. VisCAM and Magics disagree on the same word, so the convention
 * is never guessed from the bits alone.
 */
typedef enum GMDL_Stl_Color_Convention {
  GMDL_STL_COLOR_NONE = 0, ///< Attribute only; no RGB decode or pack.
  GMDL_STL_COLOR_VISCAM,   ///< Bit 15 valid=1; R, G, B in 5-bit fields.
  GMDL_STL_COLOR_MAGICS,   ///< Materialise; BGR fields; valid polarity inverted.
} GMDL_Stl_Color_Convention;

/**
 * @brief Which optional fields a triangle stated.
 */
typedef enum GMDL_Stl_Triangle_Present {
  GMDL_STL_TRI_HAS_COLOR = 1u << 0, ///< `r`, `g`, `b` were decoded or set.
} GMDL_Stl_Triangle_Present;

/**
 * @brief Which optional fields the document stated.
 */
typedef enum GMDL_Stl_Present {
  GMDL_STL_HAS_DEFAULT_COLOR = 1u << 0, ///< Magics `COLOR=` in the header.
} GMDL_Stl_Present;

/**
 * @brief One facet.
 *
 * Coordinates are as the file stated. A zero normal is recorded as zero;
 * this library does not recompute it unless a measured option asks.
 */
typedef struct GMDL_Stl_Triangle {
  float normal[3];     ///< Facet normal.
  float vertex[3][3];  ///< Three corners, each `x y z`.
  uint16_t attribute;  ///< Binary attribute word; 0 for ASCII.
  float r;             ///< Red in [0, 1] when colour is present.
  float g;             ///< Green in [0, 1] when colour is present.
  float b;             ///< Blue in [0, 1] when colour is present.
  uint32_t present;    ///< ::GMDL_Stl_Triangle_Present bits.
} GMDL_Stl_Triangle;

/**
 * @brief A parsed STL document.
 */
typedef struct GMDL_Stl {
  GMDL_Stl_Form form; ///< Encoding the file used.
  char solid_name[GMDL_STL_MAX_NAME_LENGTH]; ///< ASCII solid name, or empty.
  uint8_t header[GMDL_STL_HEADER_SIZE]; ///< Binary header bytes.
  float default_color[4]; ///< Magics `COLOR=` RGBA in [0, 1].
  uint32_t present; ///< ::GMDL_Stl_Present bits.
  GMDL_Stl_Triangle * triangles; ///< Facets, or NULL when there are none.
  size_t triangle_count;         ///< Number of facets.
  const GMDL_Allocator * allocator; ///< Allocator that owns `triangles`.
} GMDL_Stl;

/**
 * @brief Caps and readings for one STL parse or dump.
 *
 * Caps are the size_t fields and come first. Zero for `max_triangles` means
 * no cap. Zero for `max_line_length` means ::GMDL_DEFAULT_MAX_LINE_LENGTH,
 * for the same reason as OBJ and MTL: the ASCII line buffer is allocated
 * before the first line is read. A binary document ignores line length.
 *
 * A reading's zero is the behaviour documentation/stl.md states. Pass NULL
 * to a load or dump for gmdl_stl_options_default().
 */
typedef struct GMDL_Stl_Options {
  /**
   * Longest accepted ASCII line, in bytes, not counting its ending.
   *
   * Zero means ::GMDL_DEFAULT_MAX_LINE_LENGTH, not "unlimited".
   */
  size_t max_line_length;
  size_t max_triangles; ///< Cap on facets.

  /**
   * Write ASCII from ::gmdl_stl_dump().
   *
   * Zero, the default, writes binary. ASCII has no portable colour spelling;
   * the in-memory colours stay, and the file does not carry them.
   */
  bool write_ascii;
  /**
   * Parse as ASCII even when the size matches binary.
   *
   * Ignored when ::force_binary is also set; binary wins.
   */
  bool force_ascii;
  /**
   * Parse as binary even when the bytes look like `solid` text.
   */
  bool force_binary;
  /**
   * Reject `nan` and `inf` on normals and vertices.
   *
   * Zero, the default, records the value.
   */
  bool reject_non_finite;
  /**
   * How to decode and pack the per-triangle attribute as a colour.
   *
   * Zero is ::GMDL_STL_COLOR_NONE. Magics `COLOR=` in the header is always
   * recorded when seen; per-face unpacking still requires a non-none value.
   */
  GMDL_Stl_Color_Convention color_convention;
  /**
   * Replace an all-zero facet normal with the unit normal of its triangle.
   *
   * Zero, the default, records the zero. Blender, FreeCAD and OpenSCAD all
   * recompute; the presets set this.
   */
  bool recompute_zero_normals;
  /**
   * Require ASCII keywords to be lowercase.
   *
   * Zero, the default, matches case-insensitively. Blender and OpenSCAD reject
   * `SOLID` / `FACET` (empty mesh); FreeCAD's Mesh module does not load ASCII
   * STL at all in the pinned release. The Blender and OpenSCAD presets set
   * this so mixed-case keywords are ::GMDL_ERR_FORMAT.
   */
  bool require_lowercase_keywords;
} GMDL_Stl_Options;

/**
 * @brief Fill in the default STL options.
 *
 * @param options Structure to populate. NULL is ignored.
 */
GMDL_API void gmdl_stl_options_default(GMDL_Stl_Options * options);

/**
 * @brief Readings that match Blender 4.3.2's STL importer.
 *
 * Sets ::recompute_zero_normals and ::require_lowercase_keywords. Caps stay
 * at the defaults.
 *
 * @param options Structure to populate. NULL is ignored.
 */
GMDL_API void gmdl_stl_options_blender(GMDL_Stl_Options * options);

/**
 * @brief Readings that match FreeCAD 1.0.0's Mesh STL reader.
 *
 * Sets ::recompute_zero_normals. FreeCAD's Mesh module does not load ASCII
 * STL in this release, so there is no keyword-case reading to lock.
 *
 * @param options Structure to populate. NULL is ignored.
 */
GMDL_API void gmdl_stl_options_freecad(GMDL_Stl_Options * options);

/**
 * @brief Readings that match OpenSCAD 2021.01's STL importer.
 *
 * Sets ::recompute_zero_normals and ::require_lowercase_keywords.
 *
 * @param options Structure to populate. NULL is ignored.
 */
GMDL_API void gmdl_stl_options_openscad(GMDL_Stl_Options * options);

/**
 * @brief Parse an STL file from a stream.
 *
 * @param stream Stream positioned at the start of the STL data.
 * @param options Caps and readings, or NULL for gmdl_stl_options_default().
 * @param allocator Allocator for the result, or NULL for the default.
 * @param out_stl Receives the parsed model on success.
 * @return GMDL_OK, or an error code.
 */
GMDL_API GMDL_Result gmdl_stl_load(GMDL_Stream * stream,
    const GMDL_Stl_Options * options, const GMDL_Allocator * allocator,
    GMDL_Stl ** out_stl);

/**
 * @brief Parse an STL file from a path.
 *
 * @param path Path to the STL file.
 * @param options Caps and readings, or NULL for gmdl_stl_options_default().
 * @param allocator Allocator for the result, or NULL for the default.
 * @param out_stl Receives the parsed model on success.
 * @return GMDL_OK, or an error code.
 */
GMDL_API GMDL_Result gmdl_stl_load_file(const char * path,
    const GMDL_Stl_Options * options, const GMDL_Allocator * allocator,
    GMDL_Stl ** out_stl);

/**
 * @brief Free a model. NULL is ignored.
 *
 * @param stl The model.
 */
GMDL_API void gmdl_stl_free(GMDL_Stl * stl);

/**
 * @brief Write a model as STL.
 *
 * Binary by default. ASCII when @p options has `write_ascii` set, or when
 * @p options is NULL's default (binary). Pass a non-NULL options pointer to
 * select ASCII.
 *
 * @param stl The model.
 * @param options Caps and readings, or NULL for defaults (binary write).
 * @param fd Destination.
 * @return GMDL_OK, GMDL_ERR_INVALID, or GMDL_ERR_IO.
 */
GMDL_API GMDL_Result gmdl_stl_dump(const GMDL_Stl * stl,
    const GMDL_Stl_Options * options, FILE * fd);

#ifdef __cplusplus
}
#endif

#endif // GHOTI_IO_GMDL_STL_H
