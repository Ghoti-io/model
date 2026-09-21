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

  for (size_t i = 0; i < mtl->material_count; i++) {
    const GMDL_Mtl_Material * m = &mtl->materials[i];
    if (fprintf(fd, "newmtl %s\n", m->name) < 0
        || fprintf(fd, "Ka %f %f %f\n", m->Ka[0], m->Ka[1], m->Ka[2]) < 0
        || fprintf(fd, "Kd %f %f %f\n", m->Kd[0], m->Kd[1], m->Kd[2]) < 0
        || fprintf(fd, "Ks %f %f %f\n", m->Ks[0], m->Ks[1], m->Ks[2]) < 0
        || fprintf(fd, "Ns %f\n", m->Ns) < 0
        || fprintf(fd, "d %f\n", m->d) < 0
        || fprintf(fd, "illum %d\n\n", m->illum) < 0) {
      return GMDL_ERR_IO;
    }
  }
  return GMDL_OK;
}
