/**
 * @file
 *
 * The library's default allocator, which is cutil's.
 *
 * Copyright 2026 by Corey Pennycuff
 */

#include <cutil/allocator.h>
#include <ghoti.io/model/allocator.h>

const GMDL_Allocator * gmdl_allocator_default(void) {
  return gcu_allocator_default();
}
