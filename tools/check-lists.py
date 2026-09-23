#!/usr/bin/env python3
"""Fail if a map or an array is missing from a list that has to name it.

Two structures here are lists of things, and each thing has to be named again
somewhere else for the library to be correct.  Nothing about C makes those
lists agree.

A texture map in GMDL_Mtl_Material is named in four places.  Forget the free
list and the path leaks; forget the defaults list and it holds zeroes where
the format documents a scale of 1; forget the dump list and the map is
silently dropped on the way out; forget the fuzzer's comparison and the
harness cannot see that it was.

The last two are the reason this exists rather than a comment.  The fuzzer
compares a parse against a reparse, so a map absent from BOTH the dumper and
the comparison is absent symmetrically: the round trip agrees and the map is
gone.  Neither list can check the other, because each is half of the
instrument.

A builder array's initial capacity is named in two: the
gcu_array_create_in_place() call that sets it, and - implicitly - the size of
the documents the allocation-failure sweep parses.  An array that starts with
room for more records than the sweep's documents contain never reallocates
during a parse, so the arm that reports that reallocation failing is never
reached, and the sweep goes on passing with a healthy-looking count.  That
happened: the first draft reported 28 allocations and 17 fatal refusals, all
of them in obj_builder_init().

An owned array in GMDL_Obj is named in two: the steal that moves it out of
the parser's builder into the model, and the free.  A missed steal hands the
caller a NULL array for records that parsed; a missed free leaks every one of
them.  Both are easy to miss because adding an array means touching five
places and only three of them are near each other - measured by adding one.

Everything agreed when this was written.  That is what a gate is for: it
holds a state that is currently true, rather than discovering one that is
not.
"""

import re
import sys
from pathlib import Path

root = Path(__file__).resolve().parent.parent


def read(rel):
    return (root / rel).read_text()


def fail(message):
    print("check-lists: %s" % message, file=sys.stderr)
    sys.exit(1)


#
# MTL: every texture map in GMDL_Mtl_Material
#

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

#
# OBJ: every owned array in GMDL_Obj
#

obj_header = read("include/ghoti.io/model/obj.h")
obj_struct = re.search(r"typedef struct \{(.*?)\n\} GMDL_Obj;", obj_header, re.S)
if not obj_struct:
    fail("could not find GMDL_Obj in the header")

# Comments are stripped first: several of them name fields in prose, and an
# owned array is recognised by its shape - a pointer immediately followed by
# the matching `size_t <name>_count`, which is the convention the struct
# keeps and the thing that makes the pair findable at all.
obj_body = re.sub(r"/\*.*?\*/", "", obj_struct.group(1), flags=re.S)
obj_body = re.sub(r"///.*", "", obj_body)
owned = re.findall(r"\b\w+\s*\*\s*(\w+)\s*;\s*size_t\s+(\w+)_count\s*;", obj_body)
arrays_obj = {name for name, _ in owned}
if not arrays_obj:
    fail("found no owned arrays in GMDL_Obj; the pattern must have rotted")

obj_load = read("src/obj/obj_load.c")
obj_lists = {
    "the steal list (a missing entry hands back NULL for records that parsed)":
        set(re.findall(
            r"obj->(\w+)\s*=\s*obj_steal_into\(\s*&builder\.\w+,",
            obj_load)),
    "the free list (a missing entry leaks the whole array)":
        set(re.findall(r"gcu_allocator_free\(allocator, obj->(\w+)\)", obj_load)),
}
for what, got in obj_lists.items():
    missing = sorted(arrays_obj - got)
    if missing:
        problems.append("%s is missing %s" % (what, ", ".join(missing)))

#
# The allocation-failure sweep's documents against the builders' capacities
#

sweep = read("tests/unit/test_allocator.cpp")
grow = re.search(r"const size_t kGrow = (\d+);", sweep)
if not grow:
    fail("could not find kGrow in the allocation sweep; this gate is "
         "measuring nothing")
grow = int(grow.group(1))

capacities = []
empty = []
for source in ("src/obj/obj_load.c", "src/mtl/mtl_load.c"):
    text = read(source)
    for name, count in re.findall(
            r"gcu_array_create_in_place\(\s*&(?:\w+->)?(\w+)[^;]*?,"
            r"\s*(\d+),\s*allocator\)",
            text, re.S):
        if int(count) > 0:
            capacities.append((source, name, int(count)))
        else:
            empty.append((source, name))
if not capacities:
    fail("found no builder capacities at all; the pattern must have rotted")

# A capacity of zero allocates nothing, so that call's GMDL_ERR_OOM arm cannot
# run, and obj_load.c says so in a comment next to it.  Nothing else holds the
# comment to the code: give the array a starting capacity and the arm becomes
# reachable while the comment goes on claiming it is not, and the coverage
# report - which would now show a genuine gap - reads the same either way.
# That is its own failure shape, and a common one: the argument gets written
# down for the next person and stops one step short of being enforceable.
EMPTY_ON_PURPOSE = {"overflow"}
surprises = sorted({name for _, name in empty} - EMPTY_ON_PURPOSE)
if surprises:
    problems.append(
        "%s %s created with a capacity of zero, so the GMDL_ERR_OOM arm on "
        "that call cannot run and the allocation sweep will report it "
        "uncovered. Either give it a capacity or say in a comment why the "
        "check stays, and add it to EMPTY_ON_PURPOSE here"
        % (", ".join(surprises), "is" if len(surprises) == 1 else "are"))
filled = sorted(EMPTY_ON_PURPOSE - {name for _, name in empty})
if filled:
    problems.append(
        "%s no longer starts empty, so the comment in src/obj/obj_load.c "
        "saying its GMDL_ERR_OOM arm cannot run is now wrong, and the arm "
        "wants a test rather than an explanation" % ", ".join(filled))

for source, name, count in capacities:
    if count >= grow:
        problems.append(
            "%s starts %s with room for %d records, which the allocation "
            "sweep's documents (kGrow = %d) do not exceed, so that array "
            "never grows during a parse and its failure arm goes unreached"
            % (source, name, count, grow))

if problems:
    for problem in problems:
        print("check-lists: %s" % problem, file=sys.stderr)
    sys.exit(1)

print("check-lists: %d maps in %d lists, %d arrays in %d lists, all present"
      % (len(scalar), len(lists), len(arrays_obj), len(obj_lists)))
print("check-lists: %d builder capacities, largest %d, all under the sweep's "
      "kGrow of %d; %d deliberately empty"
      % (len(capacities), max(c for _, _, c in capacities), grow, len(empty)))
