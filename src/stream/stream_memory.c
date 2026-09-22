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
 * Memory-backed streams, and the file convenience that reads a whole file
 * into one.
 */

#include <ghoti.io/cutil/allocator.h>
#include <ghoti.io/cutil/file.h>
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

/**
 * Map a cutil file result onto this library's.
 *
 * GMDL_Result is the narrower of the two enumerations and is meant to stay
 * that way: a caller of gmdl_obj_load_file() acts on "the file did not
 * read", not on why it did not. So the cases named here are the ones with a
 * distinct GMDL spelling, and every other file-domain failure - a path that
 * is not there, permission refused, whatever cutil names next - is
 * GMDL_ERR_IO.
 *
 * The fallback is deliberate rather than lazy. It used to be
 * GMDL_ERR_INTERNAL, on the reasoning that an unrecognised value meant a bug
 * here. cutil's enumeration grows, so that reasoning was wrong in the way
 * that matters: it turned each addition into "internal library error" for a
 * caller whose file had simply been deleted.
 *
 * A default arm only protects the members nobody named. Two of cutil's
 * errno classifications land on members that ARE named here - a path too
 * long arrives as GCU_FILE_ERR_INVALID and an out-of-memory open as
 * GCU_FILE_ERR_OOM, both of which used to be GCU_FILE_ERR_IO - so each was
 * decided rather than inherited. See the cases.
 *
 * This assumes the caller has already rejected its own NULL arguments, which
 * gmdl_stream_create_file() does. Reusing it anywhere that has not would
 * turn a genuine argument bug into GMDL_ERR_IO.
 */
static GMDL_Result gmdl_result_from_file(GCU_File_Result result) {
  switch (result) {
    // Unreachable as things stand, and kept anyway: the one caller asks only
    // about a failure, and passes GCU_FILE_UNLIMITED so cutil has no cap to
    // exceed. Dropping either arm would leave the two most dangerous answers
    // falling into the default and coming back as GMDL_ERR_IO the moment
    // somebody calls this from a second place - which is how a mapping
    // function stops being total. Coverage therefore shows them as never
    // executed, and that is the correct state for them to be in.
    case GCU_FILE_OK:
      return GMDL_OK;
    case GCU_FILE_ERR_LIMIT:
      return GMDL_ERR_LIMIT;
    case GCU_FILE_ERR_OOM:
      return GMDL_ERR_OOM;
    case GCU_FILE_ERR_INVALID:
      // Not the same INVALID. gmdl_stream_create_file() checks its own
      // arguments before calling, so cutil cannot be telling us we passed a
      // NULL; what it reports this way is a path the filesystem will not
      // accept, which in practice means too long. Section 6 says
      // GMDL_ERR_INVALID is for a caller argument that is wrong, and a path
      // that is merely too long for one mount is not that - the same path can
      // be fine elsewhere. It is a file that did not read.
    case GCU_FILE_ERR_IO:
    default:
      return GMDL_ERR_IO;
  }
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

  // cutil reads in chunks rather than trusting a seek-to-end size, so a path
  // that names a pipe or a device still works, and it opens through the wide
  // entry point on Windows, where fopen() takes the path in the process code
  // page and therefore cannot name every file the filesystem accepts.
  void * buffer = NULL;
  size_t length = 0;
  GCU_File_Result read = gcu_file_read(
      path, GCU_FILE_UNLIMITED, allocator, &buffer, &length);
  if (read != GCU_FILE_OK) {
    return gmdl_result_from_file(read);
  }

  GMDL_Result result = gmdl_stream_create_memory_with_allocator(
      buffer, length, allocator, out_stream);
  if (result != GMDL_OK) {
    gcu_file_free(allocator, buffer);
    return result;
  }

  // Hand the buffer to the stream, which frees it on destroy. gcu_file_read
  // allocated it through the same allocator gmdl_stream_destroy releases it
  // with, which is what gcu_file_free() does.
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
