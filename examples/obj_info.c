/**
 * @file
 *
 * Print a summary of an OBJ file, and of its material library when it names
 * one that can be found next to it.
 *
 * Build with: make examples
 *
 * Copyright 2026 by Corey Pennycuff
 */

#include <stdbool.h>
#include <stdio.h>

#include <ghoti.io/cutil/path.h>
#include <ghoti.io/model/model.h>

/**
 * Join the directory part of `path` with `name`.
 *
 * Both halves come from cutil rather than a search for '/', because a
 * hand-rolled search is a POSIX assumption: under Windows rules the
 * directory part of `models\teapot.obj` ends at a backslash, and a
 * separator search that misses it would quietly look for the material
 * library in the current directory instead.
 */
static bool sibling_path(
    const char * path, const char * name, char * out, size_t out_size) {
  // Neither call truncates: a path too long for these buffers is reported as
  // GCU_PATH_ERR_LIMIT, because a truncated path still looks like a path and
  // names the wrong file.
  char dir[512];
  if (gcu_path_dirname(GCU_PATH_NATIVE, path, dir, sizeof(dir), NULL)
      != GCU_PATH_OK) {
    return false;
  }
  return gcu_path_join(GCU_PATH_NATIVE, dir, name, out, out_size, NULL)
      == GCU_PATH_OK;
}

int main(int argc, char ** argv) {
  if (argc != 2) {
    fprintf(stderr, "usage: %s <model.obj>\n", argv[0]);
    return 1;
  }

  GMDL_Obj * obj = NULL;
  GMDL_Result result = gmdl_obj_load_file(argv[1], NULL, NULL, &obj);
  if (result != GMDL_OK) {
    fprintf(stderr, "%s: %s\n", argv[1], gmdl_result_string(result));
    return 1;
  }

  printf("%s\n", argv[1]);
  printf("  vertices           %zu\n", obj->vertex_count);
  printf("  texture coords     %zu\n", obj->texcoord_count);
  printf("  normals            %zu\n", obj->normal_count);
  printf("  faces              %zu\n", obj->face_count);
  printf("  groups             %zu\n", obj->group_count);
  printf("  materials used     %zu\n", obj->material_mapping_count);

  if (obj->mtllib[0]) {
    printf("  mtllib             %s\n", obj->mtllib);

    char path[512];
    if (!sibling_path(argv[1], obj->mtllib, path, sizeof(path))) {
      printf("  (could not place %s next to %s)\n", obj->mtllib, argv[1]);
      gmdl_obj_free(obj);
      return 1;
    }

    GMDL_Mtl * mtl = NULL;
    if (gmdl_mtl_load_file(path, NULL, NULL, &mtl) == GMDL_OK) {
      printf("  materials defined  %zu\n", mtl->material_count);
      for (size_t i = 0; i < obj->material_mapping_count; i++) {
        const char * name = obj->material_mappings[i].name;
        printf("    %-20s %s\n", name,
            gmdl_mtl_find(mtl, name) ? "found" : "MISSING");
      }
      gmdl_mtl_free(mtl);
    }
    else {
      printf("  (could not read %s)\n", path);
    }
  }

  gmdl_obj_free(obj);
  return 0;
}
