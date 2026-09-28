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
 * Writing a parsed material library back out as MTL text.
 */

#include <stdio.h>

#include <ghoti.io/model/macros.h>
#include <ghoti.io/model/mtl.h>
#include "../core/number_internal.h"

/**
 * The `-type` name for a reflection slot.
 *
 * @param type The slot; ::GMDL_MTL_REFL_UNTYPED has no name and never
 *   reaches here.
 * @return The name the format uses, never NULL.
 */
static const char * gmdl_mtl_refl_type_name(GMDL_Mtl_Refl_Type type) {
  switch (type) {
    case GMDL_MTL_REFL_CUBE_TOP:
      return "cube_top";
    case GMDL_MTL_REFL_CUBE_BOTTOM:
      return "cube_bottom";
    case GMDL_MTL_REFL_CUBE_FRONT:
      return "cube_front";
    case GMDL_MTL_REFL_CUBE_BACK:
      return "cube_back";
    case GMDL_MTL_REFL_CUBE_LEFT:
      return "cube_left";
    case GMDL_MTL_REFL_CUBE_RIGHT:
      return "cube_right";
    case GMDL_MTL_REFL_SPHERE:
    case GMDL_MTL_REFL_UNTYPED:
    case GMDL_MTL_REFL_COUNT:
    default:
      // Sphere is the format's own default and the only other named slot;
      // untyped is written without a -type and does not call this.
      return "sphere";
  }
}

/**
 * Write one texture map: its directive, the options the file stated, its path.
 *
 * Only options with a bit in `present` are written, so a map that came in
 * bare goes out bare. The vector options are written in full even when the
 * file gave one or two components, because the unstated ones hold the
 * format's defaults and reparse to the same values.
 *
 * @param fd Destination.
 * @param directive The keyword, e.g. "map_Kd".
 * @param map The map. Nothing is written when its path is NULL.
 * @return 0, or -1 on a write failure.
 */
static int mtl_dump_map(
    FILE * fd, const char * directive, const GMDL_Mtl_Map * map) {
  if (!map->path) {
    return 0;
  }
  if (fprintf(fd, "%s", directive) < 0) {
    return -1;
  }
  // `-type` comes from the map's own field rather than from the `refl` slot
  // it happens to sit in. The two agree for `refl` - the slot *is* the type -
  // and only the field exists for every other map, so reading the field is
  // the one rule that covers both.
  if ((map->present & GMDL_MTL_MAP_HAS_TYPE)
      && fprintf(fd, " -type %s", gmdl_mtl_refl_type_name(map->type)) < 0) {
    return -1;
  }
  if ((map->present & GMDL_MTL_MAP_HAS_BLENDU)
      && fprintf(fd, " -blendu %s", map->blendu ? "on" : "off") < 0) {
    return -1;
  }
  if ((map->present & GMDL_MTL_MAP_HAS_BLENDV)
      && fprintf(fd, " -blendv %s", map->blendv ? "on" : "off") < 0) {
    return -1;
  }
  if ((map->present & GMDL_MTL_MAP_HAS_CLAMP)
      && fprintf(fd, " -clamp %s", map->clamp ? "on" : "off") < 0) {
    return -1;
  }
  if ((map->present & GMDL_MTL_MAP_HAS_BOOST)
      && fprintf(fd, " -boost %.9g", (double)map->boost) < 0) {
    return -1;
  }
  if ((map->present & GMDL_MTL_MAP_HAS_BM)
      && fprintf(fd, " -bm %.9g", (double)map->bm) < 0) {
    return -1;
  }
  if ((map->present & GMDL_MTL_MAP_HAS_MM)
      && fprintf(fd, " -mm %.9g %.9g", (double)map->mm[0], (double)map->mm[1])
          < 0) {
    return -1;
  }
  if ((map->present & GMDL_MTL_MAP_HAS_O)
      && fprintf(fd, " -o %.9g %.9g %.9g", (double)map->o[0],
             (double)map->o[1], (double)map->o[2])
          < 0) {
    return -1;
  }
  if ((map->present & GMDL_MTL_MAP_HAS_S)
      && fprintf(fd, " -s %.9g %.9g %.9g", (double)map->s[0],
             (double)map->s[1], (double)map->s[2])
          < 0) {
    return -1;
  }
  if ((map->present & GMDL_MTL_MAP_HAS_T)
      && fprintf(fd, " -t %.9g %.9g %.9g", (double)map->t[0],
             (double)map->t[1], (double)map->t[2])
          < 0) {
    return -1;
  }
  if ((map->present & GMDL_MTL_MAP_HAS_TEXRES)
      && fprintf(fd, " -texres %d", map->texres) < 0) {
    return -1;
  }
  if ((map->present & GMDL_MTL_MAP_HAS_IMFCHAN)
      && fprintf(fd, " -imfchan %c", "rgbmlz"[map->imfchan]) < 0) {
    return -1;
  }
  return fprintf(fd, " %s\n", map->path) < 0 ? -1 : 0;
}

/**
 * Write one colour property in the form the file used.
 *
 * RGB is three numbers. XYZ keeps the `xyz` keyword, because the three
 * numbers are not channels. Spectral writes the path and the factor only
 * when the file stated one: an omitted factor and a written 1 are different
 * lines (4.2). A spectral statement with no path writes nothing - a parse
 * cannot produce one, and a bare `Kd spectral` is a line this parser refuses.
 *
 * @param fd Destination.
 * @param name The directive, e.g. "Kd".
 * @param present The material's present mask.
 * @param bit The bit for this property.
 * @param value The three numbers.
 * @param color How they were stated.
 * @return 0, or -1 on a write failure.
 */
static int mtl_dump_color(FILE * fd, const char * name, uint32_t present,
    uint32_t bit, const float value[3], const GMDL_Mtl_Color * color) {
  if (!(present & bit)) {
    return 0;
  }
  int written = 0;
  switch (color->form) {
    case GMDL_MTL_COLOR_XYZ:
      written = fprintf(fd, "%s xyz %.9g %.9g %.9g\n", name, value[0],
          value[1], value[2]);
      break;
    case GMDL_MTL_COLOR_SPECTRAL:
      if (!color->spectral) {
        return 0;
      }
      written = color->factor_stated
          ? fprintf(fd, "%s spectral %s %.9g\n", name, color->spectral,
              color->factor)
          : fprintf(fd, "%s spectral %s\n", name, color->spectral);
      break;
    case GMDL_MTL_COLOR_RGB:
    default:
      written = fprintf(fd, "%s %.9g %.9g %.9g\n", name, value[0], value[1],
          value[2]);
      break;
  }
  return written < 0 ? -1 : 0;
}

static GMDL_Result mtl_dump_pinned(const GMDL_Mtl * mtl, FILE * fd) {
  if (!mtl || !fd) {
    return GMDL_ERR_INVALID;
  }

  // Only what the material actually states is written. Writing every field
  // regardless turned an absent property into an assertion: a material with
  // no "Kd" line became "Kd 0 0 0", which is black, where both Blender and
  // VTK read an absent one as a light default.
  for (size_t i = 0; i < mtl->material_count; i++) {
    const GMDL_Mtl_Material * m = &mtl->materials[i];
    if (fprintf(fd, "newmtl %s\n", m->name) < 0) {
      return GMDL_ERR_IO;
    }
    if (mtl_dump_color(fd, "Ka", m->present, GMDL_MTL_HAS_KA, m->Ka, &m->Ka_color)
        < 0) {
      return GMDL_ERR_IO;
    }
    if (mtl_dump_color(fd, "Kd", m->present, GMDL_MTL_HAS_KD, m->Kd, &m->Kd_color)
        < 0) {
      return GMDL_ERR_IO;
    }
    if (mtl_dump_color(fd, "Ks", m->present, GMDL_MTL_HAS_KS, m->Ks, &m->Ks_color)
        < 0) {
      return GMDL_ERR_IO;
    }
    if ((m->present & GMDL_MTL_HAS_NS)
        && fprintf(fd, "Ns %.9g\n", m->Ns) < 0) {
      return GMDL_ERR_IO;
    }
    if (m->present & GMDL_MTL_HAS_D) {
      int written = m->d_halo ? fprintf(fd, "d -halo %.9g\n", m->d)
                              : fprintf(fd, "d %.9g\n", m->d);
      if (written < 0) {
        return GMDL_ERR_IO;
      }
    }
    if ((m->present & GMDL_MTL_HAS_ILLUM)
        && fprintf(fd, "illum %d\n", m->illum) < 0) {
      return GMDL_ERR_IO;
    }
    if (mtl_dump_color(fd, "Ke", m->present, GMDL_MTL_HAS_KE, m->Ke, &m->Ke_color)
        < 0) {
      return GMDL_ERR_IO;
    }
    if (mtl_dump_color(fd, "Tf", m->present, GMDL_MTL_HAS_TF, m->Tf, &m->Tf_color)
        < 0) {
      return GMDL_ERR_IO;
    }
    if ((m->present & GMDL_MTL_HAS_NI)
        && fprintf(fd, "Ni %.9g\n", m->Ni) < 0) {
      return GMDL_ERR_IO;
    }
    if ((m->present & GMDL_MTL_HAS_TR)
        && fprintf(fd, "Tr %.9g\n", m->Tr) < 0) {
      return GMDL_ERR_IO;
    }
    if ((m->present & GMDL_MTL_HAS_PR)
        && fprintf(fd, "Pr %.9g\n", m->Pr) < 0) {
      return GMDL_ERR_IO;
    }
    if ((m->present & GMDL_MTL_HAS_PM)
        && fprintf(fd, "Pm %.9g\n", m->Pm) < 0) {
      return GMDL_ERR_IO;
    }
    if ((m->present & GMDL_MTL_HAS_PS)
        && fprintf(fd, "Ps %.9g\n", m->Ps) < 0) {
      return GMDL_ERR_IO;
    }
    if ((m->present & GMDL_MTL_HAS_PC)
        && fprintf(fd, "Pc %.9g\n", m->Pc) < 0) {
      return GMDL_ERR_IO;
    }
    if ((m->present & GMDL_MTL_HAS_PCR)
        && fprintf(fd, "Pcr %.9g\n", m->Pcr) < 0) {
      return GMDL_ERR_IO;
    }
    if ((m->present & GMDL_MTL_HAS_ANISO)
        && fprintf(fd, "aniso %.9g\n", m->aniso) < 0) {
      return GMDL_ERR_IO;
    }
    if ((m->present & GMDL_MTL_HAS_ANISOR)
        && fprintf(fd, "anisor %.9g\n", m->anisor) < 0) {
      return GMDL_ERR_IO;
    }
    if ((m->present & GMDL_MTL_HAS_SHARPNESS)
        && fprintf(fd, "sharpness %d\n", m->sharpness) < 0) {
      return GMDL_ERR_IO;
    }
    if ((m->present & GMDL_MTL_HAS_MAP_AAT)
        && fprintf(fd, "map_aat %s\n", m->map_aat ? "on" : "off") < 0) {
      return GMDL_ERR_IO;
    }
    // A map path needs no bit in `present`: NULL already says the file
    // stated none, and no file can ask for NULL. "bump" comes back out as
    // "map_bump", the spelling the format's own description leads with -
    // the two are one property here, so the round trip is of the material
    // and not of the keyword that set it.
    if (mtl_dump_map(fd, "map_Ka", &m->map_Ka) < 0
        || mtl_dump_map(fd, "map_Kd", &m->map_Kd) < 0
        || mtl_dump_map(fd, "map_Ks", &m->map_Ks) < 0
        || mtl_dump_map(fd, "map_Ns", &m->map_Ns) < 0
        || mtl_dump_map(fd, "map_d", &m->map_d) < 0
        || mtl_dump_map(fd, "map_bump", &m->map_bump) < 0
        || mtl_dump_map(fd, "map_Ke", &m->map_Ke) < 0
        || mtl_dump_map(fd, "map_Pr", &m->map_Pr) < 0
        || mtl_dump_map(fd, "map_Pm", &m->map_Pm) < 0
        || mtl_dump_map(fd, "map_Ps", &m->map_Ps) < 0
        || mtl_dump_map(fd, "norm", &m->norm) < 0
        || mtl_dump_map(fd, "disp", &m->disp) < 0
        || mtl_dump_map(fd, "decal", &m->decal) < 0) {
      return GMDL_ERR_IO;
    }
    // A cube map is six lines, so this walks the slots. The untyped slot
    // writes no -type, which is how it came in: the format wants one, and
    // inventing a surface the file never named would be worse than echoing
    // the omission.
    for (size_t j = 0; j < GMDL_MTL_REFL_COUNT; j++) {
      if (mtl_dump_map(fd, "refl", &m->refl[j]) < 0) {
        return GMDL_ERR_IO;
      }
    }
    if (fprintf(fd, "\n") < 0) {
      return GMDL_ERR_IO;
    }
  }
  return GMDL_OK;
}


/** Write an MTL document with the numeric locale pinned (number_internal.h). */
GMDL_Result gmdl_mtl_dump(const GMDL_Mtl * mtl, FILE * fd) {
  GMDL_Numeric_Scope numeric;
  gmdl_numeric_scope_begin(&numeric);
  GMDL_Result result = mtl_dump_pinned(mtl, fd);
  gmdl_numeric_scope_end(&numeric);
  return result;
}
