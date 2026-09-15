/**
 * @file
 *
 * Stream internals, shared by the stream implementation and the parsers.
 *
 * Copyright 2026 by Corey Pennycuff
 */

#ifndef GHOTI_IO_GMDL_STREAM_INTERNAL_H
#define GHOTI_IO_GMDL_STREAM_INTERNAL_H

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

#endif // GHOTI_IO_GMDL_STREAM_INTERNAL_H
