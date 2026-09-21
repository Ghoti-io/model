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
 * Stream internals, shared by the stream implementation and the parsers.
 */

#ifndef GHOTI_IO_GMDL_SRC_STREAM_STREAM_INTERNAL_H
#define GHOTI_IO_GMDL_SRC_STREAM_STREAM_INTERNAL_H

#include <ghoti.io/model/macros.h>

#include <ghoti.io/model/stream.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

struct GMDL_Stream {
  const uint8_t * data; ///< Bytes being read.
  size_t size;          ///< Number of bytes.
  size_t pos;           ///< Read offset.
  const GMDL_Allocator * allocator; ///< Allocator for the stream itself.
  void * owned; ///< The buffer, when the stream allocated it; else NULL.
};

#ifdef __cplusplus
}
#endif

#endif // GHOTI_IO_GMDL_SRC_STREAM_STREAM_INTERNAL_H
