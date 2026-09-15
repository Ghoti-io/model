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

#include <stdio.h>
#include <string.h>

#include <ghoti.io/model/model.h>

/** Join the directory part of `path` with `name`. */
static void sibling_path(
    const char * path, const char * name, char * out, size_t out_size) {
  const char * slash = strrchr(path, '/');
  if (!slash) {
    snprintf(out, out_size, "%s", name);
    return;
  }
  size_t dir_length = (size_t)(slash - path) + 1;
  snprintf(out, out_size, "%.*s%s", (int)dir_length, path, name);
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
    char path[512];
    sibling_path(argv[1], obj->mtllib, path, sizeof(path));
    printf("  mtllib             %s\n", obj->mtllib);

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
