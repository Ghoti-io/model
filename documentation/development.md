# Development

## Layout

```
include/ghoti.io/model/   Public headers
src/core/                 Result strings and the allocator
src/stream/               GMDL_Stream
src/obj/                  OBJ parsing and dumping
src/mtl/                  MTL parsing and dumping
tests/unit/               Unit tests (gtest)
tests/data/models/        Checked-in fixtures
tests/fuzz/               libFuzzer harnesses and seed corpus
```

## Adding a format

The suite convention, the same one `image` follows for codecs, is that a new
format arrives with:

1. Its own options struct, `GMDL_<Format>_Options`, holding that format's
   caps and its readings. A cap of zero means no cap, except a line-oriented
   format's `max_line_length`, where zero means
   `GMDL_DEFAULT_MAX_LINE_LENGTH`. A reading's zero is the behaviour that
   format's specification states, so a caller who leaves the field alone
   does not change it. There is no library-wide limits struct. OBJ and MTL
   each have their own, and a third format does not take either: a cap on
   `curv` means nothing to a material file, and a binary format has no line
   length. The result codes, the allocator and the stream are what a format
   shares.
2. A load function taking a `GMDL_Stream *`, that options pointer and a
   `GMDL_Allocator *`. NULL options are `gmdl_<format>_options_default()`.
   NULL allocator is the default allocator.
3. A free function that tolerates NULL.
4. Unit tests covering the happy path, the malformed cases, and every cap.
5. **A fuzz harness**, registered in the Makefile with
   `$(eval $(call fuzz-rule,fuzz_<name>,<name>))`, plus a seed in
   `tests/fuzz/corpus/<name>/`.

Point 5 is not optional. A parser over untrusted input without a fuzzer is a
parser nobody has actually tested. The first version of this section told
every loader to take `GMDL_Limits`. That struct had already become the OBJ
cap list, with two fields MTL also happened to read, and the instruction
would have handed the next format a `max_freeforms` it could not honour.

## Fuzzing

The library is rebuilt with `-fsanitize=fuzzer-no-link` rather than linking the
ordinary shared library, so libFuzzer sees the parser's branches. A harness
linked against an uninstrumented library gets no coverage signal and degrades
to generating random input.

The first byte of each input selects the caps, so the capped paths are
reachable rather than only the wide-open defaults. Keep that convention when
adding a harness - otherwise the cap checks are dead code as far as the
fuzzer is concerned. The readings stay at their defaults in that byte: the
corpus is the ordinary spelling, and a flag that changes it belongs in a
unit test.

```bash
make fuzz FUZZ_TIME=3600     # both harnesses
make fuzz-run-obj FUZZ_TIME=600
```

## Memory

Every allocation goes through the `GMDL_Allocator` the caller supplied.
`tests/unit/test_allocator.cpp` proves it with a counting allocator, including
on the error paths, which is where a parser that unwinds by hand usually leaks.

The OBJ parser builds into `GCU_Array`s and hands the finished contents to the
model with `gcu_array_steal()`, so the public struct still exposes plain
pointers and counts. The model is built last, after parsing has succeeded,
which means there is no half-built model to unwind on an error path.

## Oracles

Four references are pinned in `tools/oracle/containers/IMAGES`, in one image,
because a disagreement has to be a fact about the programs and two images
would make that fact ambiguous. Blender's importer and VTK's `vtkOBJReader`,
reached through f3d, read OBJ. FreeCAD reads it with its own `ReaderOBJ`, not
with the VTK it also links. OpenSCAD 2021.01 does not read or write OBJ; it is
pinned so a later format asks this release.

```bash
make oracle-build      # once
make oracle-version    # fail if the image is absent or the versions moved
```

`tools/oracle/oracle_run.py` asks each named reference its version, in the
image, and prints which one answered before it runs the command after `--`.
That command is still this machine's. The reference itself is reached with
`oracle_env.command()`, which is the `docker run` prefix:

```python
argv = oracle_env.command(
    "f3d", ["f3d", "--no-render", "--verbose=debug", path])
```

Blender is the same call with `blender --background --factory-startup
--python`. FreeCAD is `freecadcmd`, and OpenSCAD is `openscad`.
`GHOTI_ORACLE_MODE=host` uses binaries of the same names on this machine, and
still requires them to report the pinned versions. There is no fallback from
a missing image to those binaries: a run whose reference is not the one it
names is worse than a run that did not happen.
