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
 * Wavefront MTL material parsing.
 *
 * Reference document:
 *   http://www.paulbourke.net/dataformats/mtl/
 */

#ifndef GHOTI_IO_GMDL_MTL_H
#define GHOTI_IO_GMDL_MTL_H

#include <ghoti.io/model/core.h>
#include <ghoti.io/model/macros.h>
#include <ghoti.io/model/stream.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Longest material name retained, including the NUL. */
#define GMDL_MTL_MAX_NAME_LENGTH 128

/**
 * @brief Which properties a material's source actually stated.
 *
 * Every field of ::GMDL_Mtl_Material holds a value whether or not the file
 * said anything, so the value alone cannot distinguish "black" from "not
 * mentioned". For most properties that does not matter, because readers
 * treat the two alike. For `Kd` it matters a great deal: an absent one is a
 * light default - white in VTK, grey in Blender - and `Kd 0 0 0` is black.
 *
 * ::gmdl_mtl_dump() writes only the properties whose bit is set, so a
 * material this library read and wrote back says exactly what it was given
 * and no more. A consumer building a ::GMDL_Mtl_Material by hand sets the
 * bits for the properties it filled in; leaving `present` at 0 means "this
 * material states nothing", which dumps as a bare `newmtl`.
 */
typedef enum GMDL_Mtl_Present {
  GMDL_MTL_HAS_KA = 1u << 0,    ///< `Ka` was stated.
  GMDL_MTL_HAS_KD = 1u << 1,    ///< `Kd` was stated.
  GMDL_MTL_HAS_KS = 1u << 2,    ///< `Ks` was stated.
  GMDL_MTL_HAS_NS = 1u << 3,    ///< `Ns` was stated.
  GMDL_MTL_HAS_D = 1u << 4,     ///< `d` was stated.
  GMDL_MTL_HAS_ILLUM = 1u << 5, ///< `illum` was stated.
  GMDL_MTL_HAS_KE = 1u << 6,    ///< `Ke` was stated.
  GMDL_MTL_HAS_TF = 1u << 7,    ///< `Tf` was stated.
  GMDL_MTL_HAS_NI = 1u << 8,    ///< `Ni` was stated.
  GMDL_MTL_HAS_TR = 1u << 9,    ///< `Tr` was stated.
  GMDL_MTL_HAS_SHARPNESS = 1u << 10, ///< `sharpness` was stated.
  GMDL_MTL_HAS_PR = 1u << 11,     ///< `Pr` was stated.
  GMDL_MTL_HAS_PM = 1u << 12,     ///< `Pm` was stated.
  GMDL_MTL_HAS_PS = 1u << 13,     ///< `Ps` was stated.
  GMDL_MTL_HAS_PC = 1u << 14,     ///< `Pc` was stated.
  GMDL_MTL_HAS_PCR = 1u << 15,    ///< `Pcr` was stated.
  GMDL_MTL_HAS_ANISO = 1u << 16,  ///< `aniso` was stated.
  GMDL_MTL_HAS_ANISOR = 1u << 17, ///< `anisor` was stated.
  GMDL_MTL_HAS_MAP_AAT = 1u << 18, ///< `map_aat` was stated.
} GMDL_Mtl_Present;

/**
 * @brief Which surface a `refl` reflection map covers.
 *
 * The format's `-type` names one of seven. A `refl` line that omits `-type`
 * is kept as ::GMDL_MTL_REFL_UNTYPED rather than guessed at: the format says
 * the option is required, real files omit it anyway, and which of the seven
 * such a file meant is not something a parser can know.
 */
typedef enum GMDL_Mtl_Refl_Type {
  GMDL_MTL_REFL_UNTYPED = 0,  ///< `refl path`, with no `-type`.
  GMDL_MTL_REFL_SPHERE,       ///< `-type sphere`.
  GMDL_MTL_REFL_CUBE_TOP,     ///< `-type cube_top`.
  GMDL_MTL_REFL_CUBE_BOTTOM,  ///< `-type cube_bottom`.
  GMDL_MTL_REFL_CUBE_FRONT,   ///< `-type cube_front`.
  GMDL_MTL_REFL_CUBE_BACK,    ///< `-type cube_back`.
  GMDL_MTL_REFL_CUBE_LEFT,    ///< `-type cube_left`.
  GMDL_MTL_REFL_CUBE_RIGHT,   ///< `-type cube_right`.
  GMDL_MTL_REFL_COUNT         ///< Number of slots; not a type.
} GMDL_Mtl_Refl_Type;

/**
 * @brief Which of a texture map's options the file actually stated.
 *
 * Every option field carries the format's documented default whether or not
 * the file said so, so a consumer that ignores this mask still behaves
 * correctly. The mask is for the consumer that must tell "the file asked for
 * the default" from "the file did not ask" - a writer, mainly.
 */
typedef enum GMDL_Mtl_Map_Present {
  GMDL_MTL_MAP_HAS_BLENDU = 1u << 0,  ///< `-blendu`
  GMDL_MTL_MAP_HAS_BLENDV = 1u << 1,  ///< `-blendv`
  GMDL_MTL_MAP_HAS_BOOST = 1u << 2,   ///< `-boost`
  GMDL_MTL_MAP_HAS_MM = 1u << 3,      ///< `-mm`
  GMDL_MTL_MAP_HAS_O = 1u << 4,       ///< `-o`
  GMDL_MTL_MAP_HAS_S = 1u << 5,       ///< `-s`
  GMDL_MTL_MAP_HAS_T = 1u << 6,       ///< `-t`
  GMDL_MTL_MAP_HAS_TEXRES = 1u << 7,  ///< `-texres`
  GMDL_MTL_MAP_HAS_CLAMP = 1u << 8,   ///< `-clamp`
  GMDL_MTL_MAP_HAS_BM = 1u << 9,      ///< `-bm`
  GMDL_MTL_MAP_HAS_IMFCHAN = 1u << 10, ///< `-imfchan`
  GMDL_MTL_MAP_HAS_TYPE = 1u << 11     ///< `-type`
} GMDL_Mtl_Map_Present;

/**
 * @brief The channel `-imfchan` selects from a scalar map's image.
 */
typedef enum GMDL_Mtl_Imfchan {
  GMDL_MTL_IMFCHAN_R = 0, ///< Red.
  GMDL_MTL_IMFCHAN_G,     ///< Green.
  GMDL_MTL_IMFCHAN_B,     ///< Blue.
  GMDL_MTL_IMFCHAN_M,     ///< Matte.
  GMDL_MTL_IMFCHAN_L,     ///< Luminance - the default for a scalar map.
  GMDL_MTL_IMFCHAN_Z      ///< Depth.
} GMDL_Mtl_Imfchan;

/**
 * @brief A texture map: a path, and the options stated before it.
 *
 * `map_Kd -s 2 2 2 brick.png` is one map with a scale. Options precede the
 * path, each introduced by a leading `-`, and the path is whatever follows
 * the last of them (4.5).
 */
typedef struct GMDL_Mtl_Map {
  /** The file the map names, or NULL when the material stated no such map. */
  char * path;
  /** Bitmask of ::GMDL_Mtl_Map_Present - which options the file stated. */
  uint32_t present;
  bool blendu; ///< `-blendu`; blending in u. Default on.
  bool blendv; ///< `-blendv`; blending in v. Default on.
  bool clamp;  ///< `-clamp`; clamp rather than tile. Default off.
  float boost; ///< `-boost`; mip-map sharpening. Default 0.
  float mm[2]; ///< `-mm base gain`. Defaults 0 and 1.
  float o[3];  ///< `-o u v w`; origin offset. Defaults 0.
  float s[3];  ///< `-s u v w`; scale. Defaults 1.
  float t[3];  ///< `-t u v w`; turbulence. Defaults 0.
  int32_t texres; ///< `-texres`; resolution. Default 0.
  /**
   * `-bm`; bump multiplier. Default 1.
   *
   * Blender writes this on every normal and bump map it exports
   * (`map_Bump -bm 0.350000 nrm.png`), so a reader that refuses it refuses
   * most of what Blender produces.
   */
  float bm;
  GMDL_Mtl_Imfchan imfchan; ///< `-imfchan`. Default luminance.
  /**
   * `-type`; the surface a reflection map covers. Default untyped.
   *
   * Only `refl` acts on it - it chooses which of ::GMDL_Mtl_Refl_Type's
   * slots the path lands in (4.6). Anywhere else the option is recorded and
   * means nothing, exactly as `-bm` on a colour map is recorded and means
   * nothing. Refusing the file over it instead was the one option this
   * library still rejected, and both halves of that were wrong: Blender
   * reads such a line and keeps the texture, and a caller lost the whole
   * material library over an option that could simply be written down.
   */
  GMDL_Mtl_Refl_Type type;
} GMDL_Mtl_Map;

/**
 * @brief A single material definition.
 *
 * Each property field holds a usable value whatever the file said; `present`
 * says which of them the file actually stated. A renderer can ignore
 * `present` and get sensible material; a writer must not.
 *
 * The texture maps need no bit in `present`: a ::GMDL_Mtl_Map carries a NULL
 * `path` when the file stated no such map, because NULL is not a value any
 * file can ask for, and its own `present` mask covers its options. Paths are
 * stored exactly as the file wrote them - relative paths stay relative, and
 * no separator is translated - so resolving one against the directory the
 * `.mtl` came from is the caller's job, and the caller is the only one that
 * knows that directory.
 */
typedef struct {
  char name[GMDL_MTL_MAX_NAME_LENGTH]; ///< Material name.
  float Ka[3]; ///< Ambient colour (RGB).
  float Kd[3]; ///< Diffuse colour (RGB).
  float Ks[3]; ///< Specular colour (RGB).
  float Ns;    ///< Specular exponent.
  float d;     ///< Dissolve (opacity; 1.0 is opaque, and the default).
  int32_t illum; ///< Illumination model.
  uint32_t present; ///< Bitwise OR of ::GMDL_Mtl_Present.

  float Ke[3]; ///< Emissive colour (RGB).
  float Tf[3]; ///< Transmission filter (RGB).
  float Ni;    ///< Optical density, i.e. index of refraction.
  /**
   * Transparency, as the file stated it. **Not** folded into `d`.
   *
   * The format describes `Tr` as `1 - d`, and this library still keeps the
   * two apart, because neither reference derives one from the other: Blender
   * 4.3 ignores `Tr` outright and VTK 9.3 does too, and both take `d`
   * whichever order the pair appears in. A consumer that wants the
   * relationship can apply it, knowing from `present` which of the two its
   * file actually said; one that cannot tell them apart is stuck with a
   * value no file wrote.
   */
  float Tr;
  int32_t sharpness; ///< Reflection sharpness.
  float Pr;     ///< PBR roughness.
  float Pm;     ///< PBR metallic.
  float Ps;     ///< PBR sheen.
  float Pc;     ///< PBR clearcoat thickness.
  float Pcr;    ///< PBR clearcoat roughness.
  float aniso;  ///< PBR anisotropy.
  float anisor; ///< PBR anisotropy rotation.
  bool map_aat; ///< `map_aat on` requests texture antialiasing.

  GMDL_Mtl_Map map_Ka; ///< `map_Ka`; `path` NULL when none was stated.
  GMDL_Mtl_Map map_Kd; ///< `map_Kd`.
  GMDL_Mtl_Map map_Ks; ///< `map_Ks`.
  GMDL_Mtl_Map map_Ns; ///< `map_Ns`.
  GMDL_Mtl_Map map_d;  ///< `map_d`.
  /** `map_bump`, `bump` or `map_Bump`. The three spell one property. */
  GMDL_Mtl_Map map_bump;
  GMDL_Mtl_Map map_Ke; ///< `map_Ke`.
  GMDL_Mtl_Map map_Pr; ///< `map_Pr`.
  GMDL_Mtl_Map map_Pm; ///< `map_Pm`.
  GMDL_Mtl_Map map_Ps; ///< `map_Ps`.
  GMDL_Mtl_Map norm;   ///< `norm` - a PBR normal map.
  GMDL_Mtl_Map disp;   ///< `disp` - a displacement map.
  GMDL_Mtl_Map decal;  ///< `decal`.
  /**
   * `refl` maps, indexed by ::GMDL_Mtl_Refl_Type; each `path` NULL when
   * unstated.
   *
   * A cube map arrives as six separate `refl` lines, so this is an array
   * rather than one map: they are one property of the material stated
   * across several directives, which nothing else in MTL does.
   */
  GMDL_Mtl_Map refl[GMDL_MTL_REFL_COUNT];
} GMDL_Mtl_Material;

/**
 * @brief A parsed MTL file.
 */
typedef struct {
  GMDL_Mtl_Material * materials; ///< Materials, or NULL when there are none.
  size_t material_count;         ///< Number of materials.
  const GMDL_Allocator * allocator; ///< Allocator that owns `materials`.
} GMDL_Mtl;

/**
 * @brief Parse an MTL file from a stream.
 *
 * @param stream Stream positioned at the start of the MTL data.
 * @param limits Parsing caps, or NULL for gmdl_limits_default().
 * @param allocator Allocator for the result, or NULL for the default.
 * @param out_mtl Receives the parsed material library on success.
 * @return GMDL_OK, or GMDL_ERR_FORMAT, GMDL_ERR_LIMIT, GMDL_ERR_OOM,
 *   GMDL_ERR_UNSUPPORTED, or GMDL_ERR_INVALID.
 */
GMDL_API GMDL_Result gmdl_mtl_load(GMDL_Stream * stream,
    const GMDL_Limits * limits, const GMDL_Allocator * allocator,
    GMDL_Mtl ** out_mtl);

/**
 * @brief Parse an MTL file from a path.
 *
 * @param path Path to the MTL file.
 * @param limits Parsing caps, or NULL for gmdl_limits_default().
 * @param allocator Allocator for the result, or NULL for the default.
 * @param out_mtl Receives the parsed material library on success.
 * @return GMDL_OK, GMDL_ERR_IO if the file cannot be read, or any result
 *   gmdl_mtl_load() can return.
 */
GMDL_API GMDL_Result gmdl_mtl_load_file(const char * path,
    const GMDL_Limits * limits, const GMDL_Allocator * allocator,
    GMDL_Mtl ** out_mtl);

/**
 * @brief Free a material library returned by gmdl_mtl_load(). NULL is ignored.
 *
 * The texture map paths belong to the library too, and are freed with it, so
 * a path that must outlive the ::GMDL_Mtl has to be copied out first.
 *
 * @param mtl The material library.
 */
GMDL_API void gmdl_mtl_free(GMDL_Mtl * mtl);

/**
 * @brief Look up a material by name.
 *
 * @param mtl The material library.
 * @param name Material name to find.
 * @return The material, or NULL if there is no such material.
 */
GMDL_API const GMDL_Mtl_Material * gmdl_mtl_find(
    const GMDL_Mtl * mtl, const char * name);

/**
 * @brief Write a material library back out as MTL text.
 *
 * @param mtl The material library.
 * @param fd Destination, e.g. stdout.
 * @return GMDL_OK, GMDL_ERR_INVALID, or GMDL_ERR_IO.
 */
GMDL_API GMDL_Result gmdl_mtl_dump(const GMDL_Mtl * mtl, FILE * fd);

#ifdef __cplusplus
}
#endif

#endif // GHOTI_IO_GMDL_MTL_H
