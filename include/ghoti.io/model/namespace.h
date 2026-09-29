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
 */

#ifndef GHOTI_IO_GMDL_NAMESPACE_H
#define GHOTI_IO_GMDL_NAMESPACE_H

#include <ghoti.io/model/libver.h>

/// @cond HIDDEN_SYMBOLS

// Public types. Renamed as well as the functions, so that two versions whose
// structs differ in layout cannot be confused for one another - which is the
// whole point of the scheme, and which renaming only the functions leaves
// undone. GCU_* names are deliberately absent: they are cutil's, and cutil
// has already renamed them.
#define GMDL_Allocator GHOTIIO_MODEL(GMDL_Allocator)
#define GMDL_Mtl GHOTIIO_MODEL(GMDL_Mtl)
#define GMDL_Mtl_Options GHOTIIO_MODEL(GMDL_Mtl_Options)
#define GMDL_Mtl_Material GHOTIIO_MODEL(GMDL_Mtl_Material)
#define GMDL_Mtl_Imfchan GHOTIIO_MODEL(GMDL_Mtl_Imfchan)
#define GMDL_Mtl_Map GHOTIIO_MODEL(GMDL_Mtl_Map)
#define GMDL_Mtl_Map_Present GHOTIIO_MODEL(GMDL_Mtl_Map_Present)
#define GMDL_Mtl_Present GHOTIIO_MODEL(GMDL_Mtl_Present)
#define GMDL_Mtl_Refl_Type GHOTIIO_MODEL(GMDL_Mtl_Refl_Type)
#define GMDL_Obj GHOTIIO_MODEL(GMDL_Obj)
#define GMDL_Obj_Options GHOTIIO_MODEL(GMDL_Obj_Options)
#define GMDL_Obj_Color GHOTIIO_MODEL(GMDL_Obj_Color)
#define GMDL_Obj_Face GHOTIIO_MODEL(GMDL_Obj_Face)
#define GMDL_Obj_Face_Overflow GHOTIIO_MODEL(GMDL_Obj_Face_Overflow)
#define GMDL_Obj_Group GHOTIIO_MODEL(GMDL_Obj_Group)
#define GMDL_Obj_Line GHOTIIO_MODEL(GMDL_Obj_Line)
#define GMDL_Obj_Line_Vertex GHOTIIO_MODEL(GMDL_Obj_Line_Vertex)
#define GMDL_Obj_Material_Mapping GHOTIIO_MODEL(GMDL_Obj_Material_Mapping)
#define GMDL_Obj_Normal GHOTIIO_MODEL(GMDL_Obj_Normal)
#define GMDL_Obj_Point GHOTIIO_MODEL(GMDL_Obj_Point)
#define GMDL_Obj_Statement GHOTIIO_MODEL(GMDL_Obj_Statement)
#define GMDL_Obj_Statement_Kind GHOTIIO_MODEL(GMDL_Obj_Statement_Kind)
#define GMDL_Obj_TexCoord GHOTIIO_MODEL(GMDL_Obj_TexCoord)
#define GMDL_Obj_Vertex GHOTIIO_MODEL(GMDL_Obj_Vertex)
#define GMDL_Off GHOTIIO_MODEL(GMDL_Off)
#define GMDL_Off_Options GHOTIIO_MODEL(GMDL_Off_Options)
#define GMDL_Off_Vertex GHOTIIO_MODEL(GMDL_Off_Vertex)
#define GMDL_Off_Face GHOTIIO_MODEL(GMDL_Off_Face)
#define GMDL_Off_Vertex_Present GHOTIIO_MODEL(GMDL_Off_Vertex_Present)
#define GMDL_Off_Face_Present GHOTIIO_MODEL(GMDL_Off_Face_Present)
#define GMDL_Off_Present GHOTIIO_MODEL(GMDL_Off_Present)
#define GMDL_Result GHOTIIO_MODEL(GMDL_Result)
#define GMDL_Stl GHOTIIO_MODEL(GMDL_Stl)
#define GMDL_Stl_Options GHOTIIO_MODEL(GMDL_Stl_Options)
#define GMDL_Stl_Triangle GHOTIIO_MODEL(GMDL_Stl_Triangle)
#define GMDL_Stl_Form GHOTIIO_MODEL(GMDL_Stl_Form)
#define GMDL_Stl_Color_Convention GHOTIIO_MODEL(GMDL_Stl_Color_Convention)
#define GMDL_Stl_Triangle_Present GHOTIIO_MODEL(GMDL_Stl_Triangle_Present)
#define GMDL_Stl_Present GHOTIIO_MODEL(GMDL_Stl_Present)
#define GMDL_Stream GHOTIIO_MODEL(GMDL_Stream)

// Public functions.
#define gmdl_allocator_default GHOTIIO_MODEL(gmdl_allocator_default)
#define gmdl_mtl_dump GHOTIIO_MODEL(gmdl_mtl_dump)
#define gmdl_mtl_find GHOTIIO_MODEL(gmdl_mtl_find)
#define gmdl_mtl_free GHOTIIO_MODEL(gmdl_mtl_free)
#define gmdl_mtl_load GHOTIIO_MODEL(gmdl_mtl_load)
#define gmdl_mtl_load_file GHOTIIO_MODEL(gmdl_mtl_load_file)
#define gmdl_mtl_options_default GHOTIIO_MODEL(gmdl_mtl_options_default)
#define gmdl_mtl_options_blender GHOTIIO_MODEL(gmdl_mtl_options_blender)
#define gmdl_mtl_options_vtk GHOTIIO_MODEL(gmdl_mtl_options_vtk)
#define gmdl_obj_dump GHOTIIO_MODEL(gmdl_obj_dump)
#define gmdl_obj_free GHOTIIO_MODEL(gmdl_obj_free)
#define gmdl_obj_freeform_of_kind GHOTIIO_MODEL(gmdl_obj_freeform_of_kind)
#define gmdl_obj_load GHOTIIO_MODEL(gmdl_obj_load)
#define gmdl_obj_options_default GHOTIIO_MODEL(gmdl_obj_options_default)
#define gmdl_obj_options_freecad GHOTIIO_MODEL(gmdl_obj_options_freecad)
#define gmdl_obj_options_blender GHOTIIO_MODEL(gmdl_obj_options_blender)
#define gmdl_obj_options_vtk GHOTIIO_MODEL(gmdl_obj_options_vtk)
#define gmdl_obj_load_file GHOTIIO_MODEL(gmdl_obj_load_file)
#define gmdl_off_dump GHOTIIO_MODEL(gmdl_off_dump)
#define gmdl_off_free GHOTIIO_MODEL(gmdl_off_free)
#define gmdl_off_load GHOTIIO_MODEL(gmdl_off_load)
#define gmdl_off_load_file GHOTIIO_MODEL(gmdl_off_load_file)
#define gmdl_off_options_default GHOTIIO_MODEL(gmdl_off_options_default)
#define gmdl_stl_dump GHOTIIO_MODEL(gmdl_stl_dump)
#define gmdl_stl_free GHOTIIO_MODEL(gmdl_stl_free)
#define gmdl_stl_load GHOTIIO_MODEL(gmdl_stl_load)
#define gmdl_stl_load_file GHOTIIO_MODEL(gmdl_stl_load_file)
#define gmdl_stl_options_default GHOTIIO_MODEL(gmdl_stl_options_default)
#define gmdl_stl_options_blender GHOTIIO_MODEL(gmdl_stl_options_blender)
#define gmdl_stl_options_freecad GHOTIIO_MODEL(gmdl_stl_options_freecad)
#define gmdl_stl_options_openscad GHOTIIO_MODEL(gmdl_stl_options_openscad)
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
