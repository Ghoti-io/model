/**
 * @file
 *
 * Memory-backed streams, and the file convenience that reads a whole file
 * into one.
 *
 * Copyright 2026 by Corey Pennycuff
 */

#include <ghoti.io/cutil/allocator.h>
#include <ghoti.io/cutil/safemath.h>
#include <stdio.h>
#include <string.h>

#include <ghoti.io/model/macros.h>
#include "stream_internal.h"

GMDL_Result gmdl_stream_create_memory_with_allocator(const void * data,
    size_t size, const GMDL_Allocator * allocator, GMDL_Stream ** out_stream) {
  if (!out_stream) {
    return GMDL_ERR_INVALID;
  }
  *out_stream = NULL;
  if (!data && size) {
    return GMDL_ERR_INVALID;
  }

  GMDL_Stream * stream = gcu_allocator_calloc(allocator, 1, sizeof(*stream));
  if (!stream) {
    return GMDL_ERR_OOM;
  }

  stream->data = (const uint8_t *)data;
  stream->size = size;
  stream->pos = 0;
  stream->allocator = allocator;
  stream->owned = NULL;

  *out_stream = stream;
  return GMDL_OK;
}

GMDL_Result gmdl_stream_create_memory(
    const void * data, size_t size, GMDL_Stream ** out_stream) {
  return gmdl_stream_create_memory_with_allocator(
      data, size, NULL, out_stream);
}

GMDL_Result gmdl_stream_create_file(const char * path,
    const GMDL_Allocator * allocator, GMDL_Stream ** out_stream) {
  if (!out_stream) {
    return GMDL_ERR_INVALID;
  }
  *out_stream = NULL;
  if (!path) {
    return GMDL_ERR_INVALID;
  }

  FILE * fp = fopen(path, "rb");
  if (!fp) {
    return GMDL_ERR_IO;
  }

  // Read in chunks rather than trusting a seek-to-end size. A path can name
  // something whose length is not known ahead of time, and a file can change
  // between the measurement and the read.
  size_t capacity = 64 * 1024;
  size_t length = 0;
  uint8_t * buffer = gcu_allocator_malloc(allocator, capacity);
  if (!buffer) {
    fclose(fp);
    return GMDL_ERR_OOM;
  }

  for (;;) {
    size_t space = capacity - length;
    if (space == 0) {
      size_t grown;
      if (!gcu_safe_mul_size(capacity, 2, &grown)) {
        gcu_allocator_free(allocator, buffer);
        fclose(fp);
        return GMDL_ERR_LIMIT;
      }
      uint8_t * resized = gcu_allocator_realloc(allocator, buffer, grown);
      if (!resized) {
        gcu_allocator_free(allocator, buffer);
        fclose(fp);
        return GMDL_ERR_OOM;
      }
      buffer = resized;
      capacity = grown;
      space = capacity - length;
    }

    size_t got = fread(buffer + length, 1, space, fp);
    length += got;
    if (got < space) {
      if (ferror(fp)) {
        gcu_allocator_free(allocator, buffer);
        fclose(fp);
        return GMDL_ERR_IO;
      }
      break; // End of file.
    }
  }
  fclose(fp);

  GMDL_Result result =
      gmdl_stream_create_memory_with_allocator(buffer, length, allocator,
          out_stream);
  if (result != GMDL_OK) {
    gcu_allocator_free(allocator, buffer);
    return result;
  }

  // Hand the buffer to the stream, which frees it on destroy.
  (*out_stream)->owned = buffer;
  return GMDL_OK;
}

GMDL_Result gmdl_stream_read(
    GMDL_Stream * stream, void * buffer, size_t size, size_t * out_read) {
  if (!stream || !out_read || (!buffer && size)) {
    return GMDL_ERR_INVALID;
  }

  size_t available = stream->size - stream->pos;
  size_t take = size < available ? size : available;
  if (take) {
    memcpy(buffer, stream->data + stream->pos, take);
    stream->pos += take;
  }
  *out_read = take;
  return GMDL_OK;
}

GMDL_Result gmdl_stream_read_line(
    GMDL_Stream * stream, char * buffer, size_t size, size_t * out_length) {
  if (out_length) {
    *out_length = 0;
  }
  if (!stream || !buffer || size == 0) {
    return GMDL_ERR_INVALID;
  }
  if (stream->pos >= stream->size) {
    buffer[0] = '\0';
    return GMDL_ERR_IO; // End of stream.
  }

  size_t start = stream->pos;
  size_t end = start;
  while (end < stream->size && stream->data[end] != '\n'
      && stream->data[end] != '\r') {
    end++;
  }
  size_t length = end - start;

  // Step past the terminator, treating "\r\n" as one.
  size_t next = end;
  if (next < stream->size) {
    if (stream->data[next] == '\r') {
      next++;
      if (next < stream->size && stream->data[next] == '\n') {
        next++;
      }
    }
    else {
      next++; // '\n'
    }
  }
  stream->pos = next;

  if (length >= size) {
    // The caller's buffer cannot hold the line. Report it rather than handing
    // back a prefix that would parse as though it were the whole line.
    buffer[0] = '\0';
    return GMDL_ERR_LIMIT;
  }

  if (length) {
    memcpy(buffer, stream->data + start, length);
  }
  buffer[length] = '\0';
  if (out_length) {
    *out_length = length;
  }
  return GMDL_OK;
}

size_t gmdl_stream_tell(const GMDL_Stream * stream) {
  return stream ? stream->pos : 0;
}

size_t gmdl_stream_size(const GMDL_Stream * stream) {
  return stream ? stream->size : 0;
}

int gmdl_stream_eof(const GMDL_Stream * stream) {
  return (!stream || stream->pos >= stream->size) ? 1 : 0;
}

GMDL_Result gmdl_stream_seek(GMDL_Stream * stream, size_t offset) {
  if (!stream || offset > stream->size) {
    return GMDL_ERR_INVALID;
  }
  stream->pos = offset;
  return GMDL_OK;
}

void gmdl_stream_destroy(GMDL_Stream * stream) {
  if (!stream) {
    return;
  }
  const GMDL_Allocator * allocator = stream->allocator;
  gcu_allocator_free(allocator, stream->owned);
  gcu_allocator_free(allocator, stream);
}
