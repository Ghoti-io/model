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

#
# The flag stamps against the recipes they guard
#
# Each build tree keeps a .flags file holding the flag string it was built
# with, and the object rules depend on it, so a flag change - including one
# that arrives on the command line and touches no file - moves an mtime and
# forces a rebuild.  That only works if the stamp records the variables the
# recipes actually expand.  It did not: the release stamp recorded $(CFLAGS)
# while the library objects compile with $(LIB_CFLAGS), so changing a flag
# that lives only in LIB_CFLAGS rebuilt nothing at all.  A rebuild that does
# not happen is invisible - it looks exactly like a build already current -
# which is why this is checked here rather than left to a comment.

# Join backslash continuations first. A wrapped prerequisite list is
# indented with tabs, so without this the continuation lines read as
# recipe lines and the rule appears to expand whatever appears in them -
# order-only prerequisites like $(BUILD_DIR), which are paths and not
# flags. No rule here is wrapped today; cutil's are, and a gate that
# cries wolf the first time someone wraps a long line gets switched off.
makefile = re.sub(r"\\\n", " ", read("Makefile"))

stamp_recipes = {}
for name, body in re.findall(
        r"^\$\((\w*FLAGS_STAMP)\): force-flags\n((?:\t.*\n)+)",
        makefile, re.M):
    printf = re.search(r"printf '%s\\n' '([^']*)'", body)
    if not printf:
        fail("the %s recipe does not printf a flag string; this gate is "
             "measuring nothing" % name)
    stamp_recipes[name] = set(re.findall(r"\$\((\w+)\)", printf.group(1)))
if not stamp_recipes:
    fail("found no flag stamps at all; the pattern must have rotted")
recorded_anywhere = set().union(*stamp_recipes.values())

# Find the compile rules by what their recipes DO, not by how their targets
# are spelled.  The first version of this check took every rule whose target
# began with $(OBJ_DIR)/ as the population, and then checked how many of them
# named a stamp - so the denominator and the numerator came from the same
# pattern, and a rule spelled any other way was missing from both.  It read
# 7 of 7, clean, while $(TEST_HELPER_OBJ) sat one screen away compiling with
# $(CXXFLAGS) and naming no stamp.  A denominator drawn from the numerator's
# own pattern is not a control.
#
# Measured on a clone with the helper source present: editing CXXFLAGS
# rebuilt 9 library objects and 5 test objects and 0 helper objects, and the
# stale helper links into every test executable.  With the stamp named, 1.
#
# The second version keyed on `-c`, which is the same mistake one level down:
# a rule that compiles a source straight to an executable has no `-c` and was
# missing from the population again.  Two of them here, the examples rule and
# the fuzz harness.  Both turned out to be covered already, but only by
# accident - examples through the static archive it links, the fuzz harness
# through the stamp its objects share with it - and an accident is removed by
# an ordinary edit without anything failing.  So: a compiler variable plus any
# sign of a source, `-c` or not.  A pure link names its inputs with $^ or an
# object list and stays out.
#
# Adding them to the population is what surfaced $(CUTIL_LIBS), which is not
# incidental at all: it is pkg-config's `--libs` for a dependency, it appears
# on no compile line, and nothing recorded it.  Measured by overriding it on
# the command line - 0 of 9 objects rebuilt before, 9 of 9 after.  A
# dependency changing its link line used to relink nothing.
#
# Do not be tempted to demand a stamp on link rules as well.  A stamp on a
# rule whose recipe uses $^ is handed to the linker as an input, and it fails
# with "file format not recognized" - a prerequisite audit stays green
# through that, and only a build catches it.
compile_rules = [
    (target, prereqs, body)
    for target, prereqs, body in re.findall(
        r"^([^\s#][^\n:=]*):([^\n]*)\n((?:\t.*\n)+)", makefile, re.M)
    if re.search(r"^\t@?\$+\(\w*C(?:C|XX)\)[^\n]*(?:\s-c\s|\$<|\.c\b|\.cpp\b)",
                 body, re.M)]
if not compile_rules:
    fail("found no compile rules at all; the pattern must have rotted")

guarded = []
for target, prereqs, body in compile_rules:
    stamp = re.search(r"\$\((\w*FLAGS_STAMP)\)", prereqs)
    if stamp:
        guarded.append((stamp.group(1), prereqs, body))
    else:
        problems.append(
            "the rule for %s compiles but names no flag stamp, so it keeps "
            "whatever flags it was first built with and never rebuilds when "
            "they change. Partial coverage is worse than none: the rules that "
            "do rebuild make it look as though the flag change rebuilt "
            "everything" % target.strip())

# $(@D), $@ and $< are make's own automatic variables, not flags.
AUTOMATIC = {"@D", "@", "<", "CURDIR", "MAKE"}
for stamp, prereqs, body in guarded:
    # Only the compiler's own command line.  A recipe's mkdir and its shell
    # guards mention variables that are paths and probes, not flags, and a
    # compile-and-link line names its link inputs - which are already file
    # prerequisites, so make's mtimes cover them and the stamp need not.
    used = set()
    for line in body.splitlines():
        if re.match(r"\t@?\$+\(\w*C(?:C|XX)\)", line):
            used |= set(re.findall(r"\$\((\w+)\)", line))
    used -= AUTOMATIC
    used -= set(re.findall(r"\$\((\w+)\)", prereqs))
    missing = sorted(used - stamp_recipes.get(stamp, set()))
    if missing:
        problems.append(
            "a rule guarded by %s expands %s, which %s does not record, so "
            "changing %s rebuilds nothing"
            % (stamp, ", ".join("$(%s)" % m for m in missing), stamp,
               "them" if len(missing) > 1 else "it"))

# The link lines, which the population above deliberately excludes.
#
# A link rule cannot always take a stamp as a prerequisite: where its recipe
# uses $^ the stamp is handed to the linker as an input and the build fails
# with "file format not recognized", and a prerequisite audit stays green
# through that.  So link rules are covered transitively instead - a stamp
# change rebuilds the objects, and rebuilt objects relink whatever uses them.
#
# That only works if the link line's flags are recorded in some stamp at all,
# and three here were recorded in none: $(TESTFLAGS) on both the release and
# ASan test links, $(OS_SPECIFIC_LIBRARY_NAME_FLAG) on the shared library, and
# $(ASAN_MODELLIBRARY).  Measured: changing TESTFLAGS relinked 0 of 6 test
# binaries, with the counter armed by touching a test source to show it could
# move.  chron had the same class on $(TESTFLAGS), $(CUTIL_LIBS) and
# $(ICU_LIBS) and measured 0 of 30.
#
# This check is deliberately weaker than the one above: it asks whether a
# variable is in ANY stamp, not the right one.  Pinning a link rule to a
# particular tree's stamp means deriving the pairing from naming, which is an
# assumption about how a Makefile spells things rather than about what it
# does - exactly the kind of thing that made the first two versions of the
# check above wrong.
link_problems = []
link_lines = 0
for target, prereqs, body in re.findall(
        r"^([^\s#][^\n:=]*):([^\n]*)\n((?:\t.*\n)+)", makefile, re.M):
    for line in body.splitlines():
        if not re.match(r"\t@?\$+\(\w*C(?:C|XX)\)", line):
            continue
        if re.search(r"\s-c\s|\$<|\.c\b|\.cpp\b", line):
            continue                      # a compile; checked above
        link_lines += 1
        used = set(re.findall(r"\$\((\w+)\)", line)) - AUTOMATIC - {"^"}
        used -= set(re.findall(r"\$\((\w+)\)", prereqs))
        missing = sorted(used - recorded_anywhere)
        if missing:
            link_problems.append(
                "the link line for %s expands %s, which no flag stamp "
                "records, so changing %s relinks nothing"
                % (target.strip(), ", ".join("$(%s)" % m for m in missing),
                   "them" if len(missing) > 1 else "it"))
problems.extend(link_problems)
if not link_lines:
    fail("found no link lines at all; this check would pass vacuously")

if problems:
    for problem in problems:
        print("check-lists: %s" % problem, file=sys.stderr)
    sys.exit(1)

print("check-lists: %d maps in %d lists, %d arrays in %d lists, all present"
      % (len(scalar), len(lists), len(arrays_obj), len(obj_lists)))
print("check-lists: %d flag stamps guarding %d compile rules, each recording "
      "every variable its recipes expand; %d link lines, every flag on them "
      "recorded somewhere"
      % (len(stamp_recipes), len(guarded), link_lines))
print("check-lists: %d builder capacities, largest %d, all under the sweep's "
      "kGrow of %d; %d deliberately empty"
      % (len(capacities), max(c for _, _, c in capacities), grow, len(empty)))
