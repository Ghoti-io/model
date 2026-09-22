#!/usr/bin/env python3
"""Fail if any texture map in GMDL_Mtl_Material is missing from a list.

A map is named in five places that must agree, and nothing makes them.  Add
one to the struct and forget the free list and it leaks; forget the defaults
list and it holds zeroes where the format documents a scale of 1; forget the
dump list and the map is silently dropped on the way out; forget the fuzzer's
comparison and the harness cannot see that it was.

The last two are the reason this exists rather than a comment.  The fuzzer
compares a parse against a reparse, so a map absent from BOTH the dumper and
the comparison is absent symmetrically: the round trip agrees and the map is
gone.  Neither list can check the other.

All five agreed when this was written.  That is what a gate is for - it holds
a state that is currently true, rather than discovering one that is not.
"""

import re
import sys
from pathlib import Path

root = Path(__file__).resolve().parent.parent


def read(rel):
    return (root / rel).read_text()


def fail(message):
    print("check-map-lists: %s" % message, file=sys.stderr)
    sys.exit(1)


header = read("include/ghoti.io/model/mtl.h")
material = re.search(
    r"typedef struct \{(.*?)\} GMDL_Mtl_Material;", header, re.S)
if not material:
    fail("could not find GMDL_Mtl_Material in the header")

declared = re.findall(
    r"GMDL_Mtl_Map\s+(\w+)\s*(\[[^\]]*\])?\s*;", material.group(1))
scalar = {name for name, array in declared if not array}
arrays = {name for name, array in declared if array}
if not scalar:
    fail("found no GMDL_Mtl_Map fields at all; the pattern must have rotted")

load = read("src/mtl/mtl_load.c")
dump = read("src/mtl/mtl_dump.c")
fuzz = read("tests/fuzz/fuzz_mtl.cpp")


def block(text, pattern, what):
    found = re.search(pattern, text, re.S)
    if not found:
        fail("could not find the %s; this gate is measuring nothing" % what)
    return found.group(1)


lists = {
    "the free list (a missing entry leaks the path)":
        set(re.findall(r"allocator,\s*material->(\w+)\.path", load)),
    "the defaults list (a missing entry holds zeroes, not the documented "
    "defaults)":
        set(re.findall(r"material->(\w+)", block(
            load, r"GMDL_Mtl_Map \* maps\[\] = \{(.*?)\};", "defaults list"))),
    "the dump list (a missing entry is dropped on the way out)":
        set(re.findall(r'mtl_dump_map\(fd, "[^"]*", &m->(\w+)\)', dump)),
    "the fuzzer's comparison (a missing entry hides a dumper that drops it)":
        set(re.findall(r"m->(\w+)", block(
            fuzz, r"const GMDL_Mtl_Map \* const fixed\[\] = \{(.*?)\};",
            "fuzzer map list"))),
}

problems = []
for what, got in lists.items():
    missing = sorted(scalar - got)
    unexpected = sorted(got - scalar - arrays)
    if missing:
        problems.append("%s is missing %s" % (what, ", ".join(missing)))
    if unexpected:
        problems.append("%s names %s, which is not a map field"
                        % (what, ", ".join(unexpected)))

if problems:
    for problem in problems:
        print("check-map-lists: %s" % problem, file=sys.stderr)
    sys.exit(1)

print("check-map-lists: %d maps, all present in all %d lists"
      % (len(scalar), len(lists)))
