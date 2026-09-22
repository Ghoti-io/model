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
    if (fprintf(fd, "\n") < 0) {
      return GMDL_ERR_IO;
    }
  }
  return GMDL_OK;
}
