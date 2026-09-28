# Development

## Layout

```
include/ghoti.io/model/   Public headers
src/core/                 Result strings, limits, the allocator
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

1. A load function taking a `GMDL_Stream *`, a `GMDL_Limits *` and a
   `GMDL_Allocator *`, each of the last two accepting NULL.
2. A free function that tolerates NULL.
3. Unit tests covering the happy path, the malformed cases, and the limits.
4. **A fuzz harness**, registered in the Makefile with
   `$(eval $(call fuzz-rule,fuzz_<name>,<name>))`, plus a seed in
   `tests/fuzz/corpus/<name>/`.

Point 4 is not optional. A parser over untrusted input without a fuzzer is a
parser nobody has actually tested.

## Fuzzing

The library is rebuilt with `-fsanitize=fuzzer-no-link` rather than linking the
ordinary shared library, so libFuzzer sees the parser's branches. A harness
linked against an uninstrumented library gets no coverage signal and degrades
to generating random input.

The first byte of each input selects the limits, so the capped paths are
reachable rather than only the wide-open defaults. Keep that convention when
adding a harness - otherwise the limit checks are dead code as far as the
fuzzer is concerned.

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

Two other OBJ readers are how a reading of the format gets checked when the
specification and the file disagree: Blender's importer, and VTK's
`vtkOBJReader` reached through f3d. They are pinned together in
`tools/oracle/containers/IMAGES`, in one image, because the useful fact is
where they disagree and two images would make that fact ambiguous.

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
--python`. `GHOTI_ORACLE_MODE=host` uses binaries of the same names on this
machine, and still requires them to report the pinned versions. There is no
fallback from a missing image to those binaries: a run whose reference is not
the one it names is worse than a run that did not happen.
