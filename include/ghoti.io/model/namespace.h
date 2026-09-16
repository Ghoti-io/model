/**
 * @file namespace.h
 *
 * Maps every public name of this library into its version namespace.
 *
 * Generated from the built library's dynamic symbol table and kept in one file
 * rather than beside each declaration: a type rename has to be in effect
 * before any struct tag that uses the name, and an internal header may define
 * such a tag without including the public header that declares the typedef.
 *
 * `make check-symbols` fails if an exported symbol is missing from this list.
 *
 * See CONVENTIONS.md section 4.
 *
 * Copyright 2026 by Corey Pennycuff
 */

#ifndef GHOTI_IO_GMDL_NAMESPACE_H
#define GHOTI_IO_GMDL_NAMESPACE_H

#include <ghoti.io/model/libver.h>

/// @cond HIDDEN_SYMBOLS
#define gmdl_allocator_default GHOTIIO_MODEL(gmdl_allocator_default)
#define gmdl_limits_default GHOTIIO_MODEL(gmdl_limits_default)
#define gmdl_mtl_dump GHOTIIO_MODEL(gmdl_mtl_dump)
#define gmdl_mtl_find GHOTIIO_MODEL(gmdl_mtl_find)
#define gmdl_mtl_free GHOTIIO_MODEL(gmdl_mtl_free)
#define gmdl_mtl_load GHOTIIO_MODEL(gmdl_mtl_load)
#define gmdl_mtl_load_file GHOTIIO_MODEL(gmdl_mtl_load_file)
#define gmdl_obj_dump GHOTIIO_MODEL(gmdl_obj_dump)
#define gmdl_obj_free GHOTIIO_MODEL(gmdl_obj_free)
#define gmdl_obj_load GHOTIIO_MODEL(gmdl_obj_load)
#define gmdl_obj_load_file GHOTIIO_MODEL(gmdl_obj_load_file)
#define gmdl_result_string GHOTIIO_MODEL(gmdl_result_string)
#define gmdl_stream_create_file GHOTIIO_MODEL(gmdl_stream_create_file)
#define gmdl_stream_create_memory GHOTIIO_MODEL(gmdl_stream_create_memory)
#define gmdl_stream_create_memory_with_allocator GHOTIIO_MODEL(gmdl_stream_create_memory_with_allocator)
#define gmdl_stream_destroy GHOTIIO_MODEL(gmdl_stream_destroy)
#define gmdl_stream_eof GHOTIIO_MODEL(gmdl_stream_eof)
#define gmdl_stream_read GHOTIIO_MODEL(gmdl_stream_read)
#define gmdl_stream_read_line GHOTIIO_MODEL(gmdl_stream_read_line)
#define gmdl_stream_seek GHOTIIO_MODEL(gmdl_stream_seek)
#define gmdl_stream_size GHOTIIO_MODEL(gmdl_stream_size)
#define gmdl_stream_tell GHOTIIO_MODEL(gmdl_stream_tell)
/// @endcond

#endif // GHOTI_IO_GMDL_NAMESPACE_H
