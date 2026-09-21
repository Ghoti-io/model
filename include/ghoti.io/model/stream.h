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
 * Byte-stream abstraction for the Ghoti.io Model library.
 *
 * Parsers read through a GMDL_Stream rather than a FILE *, which is what lets
 * the same code serve a file on disk, a buffer already in memory, and a fuzz
 * harness's input without a temporary file in the middle.
 */

#ifndef GHOTI_IO_GMDL_STREAM_H
#define GHOTI_IO_GMDL_STREAM_H

#include <ghoti.io/model/core.h>
#include <ghoti.io/model/macros.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Opaque read stream.
 */
typedef struct GMDL_Stream GMDL_Stream;

/**
 * @brief Create a stream over a caller-owned buffer.
 *
 * The buffer is borrowed, not copied: it must outlive the stream.
 *
 * @param data Bytes to read. May be NULL only when `size` is 0.
 * @param size Number of bytes.
 * @param out_stream Receives the new stream on success.
 * @return GMDL_OK, GMDL_ERR_INVALID, or GMDL_ERR_OOM.
 */
GMDL_API GMDL_Result gmdl_stream_create_memory(
    const void * data, size_t size, GMDL_Stream ** out_stream);

/**
 * @brief Create a stream over a caller-owned buffer, using a given allocator.
 *
 * @param data Bytes to read. May be NULL only when `size` is 0.
 * @param size Number of bytes.
 * @param allocator Allocator for the stream object. NULL uses the default.
 * @param out_stream Receives the new stream on success.
 * @return GMDL_OK, GMDL_ERR_INVALID, or GMDL_ERR_OOM.
 */
GMDL_API GMDL_Result gmdl_stream_create_memory_with_allocator(
    const void * data, size_t size, const GMDL_Allocator * allocator,
    GMDL_Stream ** out_stream);

/**
 * @brief Create a stream over the contents of a file.
 *
 * The file is read into memory owned by the stream and closed before this
 * returns, so the stream never holds a file descriptor.
 *
 * @param path Path to the file.
 * @param allocator Allocator for the stream and its buffer. NULL uses the
 *   default.
 * @param out_stream Receives the new stream on success.
 * @return GMDL_OK, GMDL_ERR_INVALID, GMDL_ERR_IO, or GMDL_ERR_OOM.
 */
GMDL_API GMDL_Result gmdl_stream_create_file(const char * path,
    const GMDL_Allocator * allocator, GMDL_Stream ** out_stream);

/**
 * @brief Read up to `size` bytes.
 *
 * Reading fewer bytes than asked for is not an error; it means the stream is
 * at its end. Check `*out_read`.
 *
 * @param stream The stream.
 * @param buffer Destination.
 * @param size Maximum bytes to read.
 * @param out_read Receives the number of bytes actually read. Required.
 * @return GMDL_OK or GMDL_ERR_INVALID.
 */
GMDL_API GMDL_Result gmdl_stream_read(
    GMDL_Stream * stream, void * buffer, size_t size, size_t * out_read);

/**
 * @brief Read one line, without its terminator.
 *
 * A line ends at "\n", "\r\n", "\r", or the end of the stream. The line is
 * written to `buffer` and always NUL-terminated when `size` is non-zero. A
 * line longer than `size - 1` is an error rather than a silent truncation, so
 * that a file of one enormous line cannot masquerade as valid input; the
 * stream is left positioned after that line either way.
 *
 * @param stream The stream.
 * @param buffer Destination.
 * @param size Size of `buffer`, including room for the NUL.
 * @param out_length Receives the line length, excluding the NUL. Optional.
 * @return GMDL_OK on a line, GMDL_ERR_IO at end of stream, GMDL_ERR_LIMIT for
 *   a line that does not fit, or GMDL_ERR_INVALID.
 */
GMDL_API GMDL_Result gmdl_stream_read_line(
    GMDL_Stream * stream, char * buffer, size_t size, size_t * out_length);

/**
 * @brief Current read offset.
 *
 * @param stream The stream. NULL returns 0.
 * @return Bytes consumed so far.
 */
GMDL_API size_t gmdl_stream_tell(const GMDL_Stream * stream);

/**
 * @brief Total size of the stream.
 *
 * @param stream The stream. NULL returns 0.
 * @return Size in bytes.
 */
GMDL_API size_t gmdl_stream_size(const GMDL_Stream * stream);

/**
 * @brief Whether the stream has been fully consumed.
 *
 * @param stream The stream. NULL returns 1.
 * @return Non-zero at end of stream.
 */
GMDL_API int gmdl_stream_eof(const GMDL_Stream * stream);

/**
 * @brief Seek to an absolute offset.
 *
 * @param stream The stream.
 * @param offset Offset from the start, at most gmdl_stream_size().
 * @return GMDL_OK or GMDL_ERR_INVALID.
 */
GMDL_API GMDL_Result gmdl_stream_seek(GMDL_Stream * stream, size_t offset);

/**
 * @brief Destroy a stream. NULL is ignored.
 *
 * @param stream The stream.
 */
GMDL_API void gmdl_stream_destroy(GMDL_Stream * stream);

#ifdef __cplusplus
}
#endif

#endif // GHOTI_IO_GMDL_STREAM_H
