# Ghoti.io Model

Wavefront geometry and materials, read through a byte stream. The same call
serves a file, a buffer already in memory, and a fuzzer's input.

## Formats

This is what the library implements.

- Wavefront OBJ.
- Wavefront MTL.

Both load, and both can be written back out. Which directives are kept is
in [documentation/obj-mtl.md](documentation/obj-mtl.md).

## Before you call it

- Every load takes that format's options pointer and a `GMDL_Allocator *`. `NULL` is the defaults. OBJ's is `GMDL_Obj_Options`; MTL's is `GMDL_Mtl_Options`. A later format brings its own.
- The only cap with a value by default is `max_line_length` (64 KiB). The parser reads a line at a time, so without it one unterminated line would be read in full. The record caps default to no limit, because the size of the input already bounds them. A reading's zero is the behaviour the specification states.
- Named combinations that match a measured reader are filled by `gmdl_obj_options_freecad()`, `gmdl_obj_options_blender()`, and `gmdl_obj_options_vtk()` (and the MTL blender/vtk helpers). Those only write the individual reading fields; the loader never asks for an oracle by name.
- A line longer than the buffer is `GMDL_ERR_LIMIT`. The reader does not hand back a prefix: the rest of a split line would be parsed as a line of its own, and a face cut in half would become a second face.
- An index that names a vertex the file does not contain is recorded, not rejected. Readers disagree about that case, and the check belongs with the consumer that is about to use the index.

```c
GMDL_Obj_Options options;
gmdl_obj_options_default(&options);
options.max_vertices = 1u << 20;
options.max_faces = 1u << 20;
```

## Examples

```c
#include <ghoti.io/model/model.h>
#include <stdio.h>

int main(void) {
  GMDL_Obj * obj = NULL;
  if (gmdl_obj_load_file("teapot.obj", NULL, NULL, &obj) != GMDL_OK) {
    return 1;
  }
  printf("%zu vertices, %zu faces\n", obj->vertex_count, obj->face_count);
  gmdl_obj_free(obj);
  return 0;
}
```

The counts it prints are the file's. From memory, open a stream and pass it
to `gmdl_obj_load()`:

```c
GMDL_Stream * stream = NULL;
gmdl_stream_create_memory(bytes, length, &stream);

GMDL_Obj * obj = NULL;
GMDL_Result result = gmdl_obj_load(stream, NULL, NULL, &obj);
gmdl_stream_destroy(stream);
```

`examples/obj_info.c` also loads the material library named beside the file.

## Compile and link

Once the library is installed, pkg-config carries the include path, the
library, and its dependencies:

```bash
cc -o show show.c $(pkg-config --cflags --libs ghoti.io-model-0)
```

The module name ends in the major version, `-0` for this release, so two
majors can be installed side by side. A build made with `make BRANCH=-dev`
installs `ghoti.io-model-dev` instead.

## Building the library

[cutil](https://github.com/Ghoti-io/cutil) must already be installed where
pkg-config can see it. A dependency it cannot find is a hard error naming
the fix.

```bash
make
make test
sudo make install
```

From the parent of a suite checkout:

```bash
./suite/install.sh
export PKG_CONFIG_PATH="$PWD/.local/share/pkgconfig"
make -C libs/model test PREFIX="$PWD/.local"
```

`make test` is the suite. `make help` lists the rest, including
`make test-asan` and `make test-valgrind`.

| Target | What it does |
| --- | --- |
| `make examples` | `examples/obj_info.c` |
| `make coverage` | Line coverage, per file |
| `make fuzz` | Build and run the OBJ and MTL fuzzers |
| `make docs` | The Doxygen manual, into `./docs` |

## The API

Everything is prefixed `gmdl_` / `GMDL_`, under `<ghoti.io/model/...>`.

- **`core.h`** — `GMDL_Result` and `gmdl_result_string()`.
- **`stream.h`** — a byte stream over memory or a file, with a line reader.
- **`obj.h`** — `GMDL_Obj_Options`, `gmdl_obj_load()`, `gmdl_obj_load_file()`, `gmdl_obj_free()`, `gmdl_obj_dump()`.
- **`mtl.h`** — `GMDL_Mtl_Options`, `gmdl_mtl_load()`, `gmdl_mtl_load_file()`, `gmdl_mtl_free()`, `gmdl_mtl_find()`, `gmdl_mtl_dump()`.
- **`allocator.h`** — `GMDL_Allocator`, which is cutil's `GCU_Allocator`.

[Formats](#formats) is what is implemented.
[Before you call it](#before-you-call-it) is what that changes about a call.

## Dependencies

Found through pkg-config, and the installed `.pc` file names it, so a
program that links `ghoti.io-model-0` links this too.

- [ghoti.io-cutil](https://github.com/Ghoti-io/cutil) — the allocator.

## Documentation

[documentation/obj-mtl.md](documentation/obj-mtl.md) is what the parsers
accept, what they record, and what a caller still has to check. `make docs`
builds the manual.

## Status

OBJ and MTL load, and both can be written back out.

## License

LGPL-3.0-only. See [COPYING.LESSER](COPYING.LESSER) for the license, and
[COPYING](COPYING) for the GPL text it is written as additional permissions
on top of.

Contributions are not being accepted at this time; see
[CONTRIBUTING.md](CONTRIBUTING.md) for what is useful instead.
