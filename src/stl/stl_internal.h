/*
 * SPDX-License-Identifier: LGPL-3.0-only
 *
 * Copyright (C) 2026 Corey Pennycuff
 *
 * This file is part of Ghoti.io Model.
 *
 * Internal helpers shared by the STL loader and dumper.
 */

#ifndef GHOTI_IO_GMDL_SRC_STL_INTERNAL_H
#define GHOTI_IO_GMDL_SRC_STL_INTERNAL_H

#include <ghoti.io/model/macros.h>
#include <ghoti.io/model/stl.h>
#include <stdbool.h>
#include <stdint.h>

/**
 * Pack RGB in [0, 1] into an attribute word under @p convention.
 *
 * ::GMDL_STL_COLOR_NONE returns 0.
 */
static inline uint16_t stl_pack_color(GMDL_Stl_Color_Convention convention,
    float r, float g, float b) {
  uint16_t ri = 0;
  uint16_t gi = 0;
  uint16_t bi = 0;
  if (r > 0.0f) {
    ri = (uint16_t)(r >= 1.0f ? 31 : (int)(r * 31.0f + 0.5f));
  }
  if (g > 0.0f) {
    gi = (uint16_t)(g >= 1.0f ? 31 : (int)(g * 31.0f + 0.5f));
  }
  if (b > 0.0f) {
    bi = (uint16_t)(b >= 1.0f ? 31 : (int)(b * 31.0f + 0.5f));
  }
  if (convention == GMDL_STL_COLOR_VISCAM) {
    return (uint16_t)((1u << 15) | (ri << 10) | (gi << 5) | bi);
  }
  if (convention == GMDL_STL_COLOR_MAGICS) {
    return (uint16_t)((bi << 10) | (gi << 5) | ri);
  }
  return 0;
}

/**
 * Decode a live attribute word into RGB in [0, 1].
 *
 * @return true when the word is live under @p convention.
 */
static inline bool stl_decode_color(GMDL_Stl_Color_Convention convention,
    uint16_t attribute, float * r, float * g, float * b) {
  const float scale = 1.0f / 31.0f;
  if (convention == GMDL_STL_COLOR_VISCAM) {
    if ((attribute & (1u << 15)) == 0) {
      return false;
    }
    *r = ((attribute >> 10) & 0x1fu) * scale;
    *g = ((attribute >> 5) & 0x1fu) * scale;
    *b = (attribute & 0x1fu) * scale;
    return true;
  }
  if (convention == GMDL_STL_COLOR_MAGICS) {
    if ((attribute & (1u << 15)) != 0) {
      return false;
    }
    *b = ((attribute >> 10) & 0x1fu) * scale;
    *g = ((attribute >> 5) & 0x1fu) * scale;
    *r = (attribute & 0x1fu) * scale;
    return true;
  }
  return false;
}

#endif // GHOTI_IO_GMDL_SRC_STL_INTERNAL_H
