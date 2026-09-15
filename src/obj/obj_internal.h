/**
 * @file
 *
 * Helpers shared by the OBJ and MTL parsers.
 *
 * Copyright 2026 by Corey Pennycuff
 */

#ifndef GHOTI_IO_GMDL_OBJ_INTERNAL_H
#define GHOTI_IO_GMDL_OBJ_INTERNAL_H

#include <ghoti.io/model/core.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Match a directive at the start of a line.
 *
 * A directive ends at whitespace or at the end of the line, so that "usemtlx"
 * is not read as "usemtl" with an argument of "x" - which is what a plain
 * length-limited comparison did.
 *
 * @param line The line.
 * @param directive The keyword to match.
 * @param out_rest Receives a pointer to the first character after the
 *   directive and any whitespace following it. Optional.
 * @return true when the line begins with the directive.
 */
bool gmdl_line_is(
    const char * line, const char * directive, const char ** out_rest);

/**
 * Whether a limit is exceeded. A limit of 0 means "no limit".
 *
 * @param count The count that is about to be increased by one.
 * @param limit The cap, or 0.
 * @return true when appending would exceed the cap.
 */
bool gmdl_limit_reached(size_t count, size_t limit);

#ifdef __cplusplus
}
#endif

#endif // GHOTI_IO_GMDL_OBJ_INTERNAL_H
