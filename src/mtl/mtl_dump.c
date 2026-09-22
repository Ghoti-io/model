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

GMDL_Result gmdl_mtl_dump(const GMDL_Mtl * mtl, FILE * fd) {
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
    if ((m->present & GMDL_MTL_HAS_KA)
        && fprintf(fd, "Ka %.9g %.9g %.9g\n", m->Ka[0], m->Ka[1], m->Ka[2])
            < 0) {
      return GMDL_ERR_IO;
    }
    if ((m->present & GMDL_MTL_HAS_KD)
        && fprintf(fd, "Kd %.9g %.9g %.9g\n", m->Kd[0], m->Kd[1], m->Kd[2])
            < 0) {
      return GMDL_ERR_IO;
    }
    if ((m->present & GMDL_MTL_HAS_KS)
        && fprintf(fd, "Ks %.9g %.9g %.9g\n", m->Ks[0], m->Ks[1], m->Ks[2])
            < 0) {
      return GMDL_ERR_IO;
    }
    if ((m->present & GMDL_MTL_HAS_NS)
        && fprintf(fd, "Ns %.9g\n", m->Ns) < 0) {
      return GMDL_ERR_IO;
    }
    if ((m->present & GMDL_MTL_HAS_D) && fprintf(fd, "d %.9g\n", m->d) < 0) {
      return GMDL_ERR_IO;
    }
    if ((m->present & GMDL_MTL_HAS_ILLUM)
        && fprintf(fd, "illum %d\n", m->illum) < 0) {
      return GMDL_ERR_IO;
    }
    if ((m->present & GMDL_MTL_HAS_KE)
        && fprintf(fd, "Ke %.9g %.9g %.9g\n", m->Ke[0], m->Ke[1], m->Ke[2])
            < 0) {
      return GMDL_ERR_IO;
    }
    if ((m->present & GMDL_MTL_HAS_TF)
        && fprintf(fd, "Tf %.9g %.9g %.9g\n", m->Tf[0], m->Tf[1], m->Tf[2])
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
    if (m->map_Ka && fprintf(fd, "map_Ka %s\n", m->map_Ka) < 0) {
      return GMDL_ERR_IO;
    }
    if (m->map_Kd && fprintf(fd, "map_Kd %s\n", m->map_Kd) < 0) {
      return GMDL_ERR_IO;
    }
    if (m->map_Ks && fprintf(fd, "map_Ks %s\n", m->map_Ks) < 0) {
      return GMDL_ERR_IO;
    }
    if (m->map_Ns && fprintf(fd, "map_Ns %s\n", m->map_Ns) < 0) {
      return GMDL_ERR_IO;
    }
    if (m->map_d && fprintf(fd, "map_d %s\n", m->map_d) < 0) {
      return GMDL_ERR_IO;
    }
    if (m->map_bump && fprintf(fd, "map_bump %s\n", m->map_bump) < 0) {
      return GMDL_ERR_IO;
    }
    if (m->map_Ke && fprintf(fd, "map_Ke %s\n", m->map_Ke) < 0) {
      return GMDL_ERR_IO;
    }
    if (m->map_Pr && fprintf(fd, "map_Pr %s\n", m->map_Pr) < 0) {
      return GMDL_ERR_IO;
    }
    if (m->map_Pm && fprintf(fd, "map_Pm %s\n", m->map_Pm) < 0) {
      return GMDL_ERR_IO;
    }
    if (m->map_Ps && fprintf(fd, "map_Ps %s\n", m->map_Ps) < 0) {
      return GMDL_ERR_IO;
    }
    if (m->norm && fprintf(fd, "norm %s\n", m->norm) < 0) {
      return GMDL_ERR_IO;
    }
    if (m->disp && fprintf(fd, "disp %s\n", m->disp) < 0) {
      return GMDL_ERR_IO;
    }
    if (m->decal && fprintf(fd, "decal %s\n", m->decal) < 0) {
      return GMDL_ERR_IO;
    }
    // A cube map is six lines, so this walks the slots. The untyped slot
    // writes no -type, which is how it came in: the format wants one, and
    // inventing a surface the file never named would be worse than echoing
    // the omission.
    for (size_t j = 0; j < GMDL_MTL_REFL_COUNT; j++) {
      if (!m->refl[j]) {
        continue;
      }
      int written = j == GMDL_MTL_REFL_UNTYPED
          ? fprintf(fd, "refl %s\n", m->refl[j])
          : fprintf(fd, "refl -type %s %s\n", gmdl_mtl_refl_type_name(
                (GMDL_Mtl_Refl_Type)j), m->refl[j]);
      if (written < 0) {
        return GMDL_ERR_IO;
      }
    }
    if (fprintf(fd, "\n") < 0) {
      return GMDL_ERR_IO;
    }
  }
  return GMDL_OK;
}
