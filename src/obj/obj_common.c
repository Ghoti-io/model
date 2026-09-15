/**
 * @file
 *
 * Helpers shared by the OBJ and MTL parsers.
 *
 * Copyright 2026 by Corey Pennycuff
 */

#include <string.h>

#include "obj_internal.h"

bool gmdl_line_is(
    const char * line, const char * directive, const char ** out_rest) {
  size_t length = strlen(directive);
  if (strncmp(line, directive, length) != 0) {
    return false;
  }

  const char * rest = line + length;
  if (*rest != '\0' && *rest != ' ' && *rest != '\t') {
    return false;
  }
  while (*rest == ' ' || *rest == '\t') {
    rest++;
  }
  if (out_rest) {
    *out_rest = rest;
  }
  return true;
}

bool gmdl_limit_reached(size_t count, size_t limit) {
  return limit != 0 && count >= limit;
}
