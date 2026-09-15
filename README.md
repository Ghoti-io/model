# Ghoti.io Model

3D model and material parsing for the Ghoti.io suite: Wavefront **OBJ**
geometry and **MTL** materials.

The parsers read through a byte stream rather than a file path, so the same
code serves a file on disk, a buffer already in memory, and a fuzz harness's
input without a temporary file in the middle.

```c
#include <ghoti.io/model/model.h>

GMDL_Obj * obj = NULL;
if (gmdl_obj_load_file("teapot.obj", NULL, NULL, &obj) == GMDL_OK) {
  printf("%zu vertices, %zu faces\n", obj->vertex_count, obj->face_count);
  gmdl_obj_free(obj);
}
```

Parsing from memory is the same call with a stream:

```c
GMDL_Stream * stream = NULL;
gmdl_stream_create_memory(bytes, length, &stream);

GMDL_Obj * obj = NULL;
GMDL_Result result = gmdl_obj_load(stream, NULL, NULL, &obj);
gmdl_stream_destroy(stream);
```

## Where this came from

OBJ and MTL parsing lived in [cjelly](https://github.com/Ghoti-io/cjelly), the
Vulkan engine, because that was where the first consumer was going to be. They
depended on nothing from it - no Vulkan, no engine types, only libc and
[cutil](https://github.com/Ghoti-io/cutil) - and nothing in cjelly outside
their own tests called them.

Sitting there had a cost. cjelly needs a GPU and a window, so it is the one
library in the suite that cannot be tested properly, and two pure text parsers
over untrusted input were inheriting that. Here they get what the other format
libraries get: a stream API, a fuzz harness each, a coverage target, and a
sanitizer build.

## Building

Requires [cutil](https://github.com/Ghoti-io/cutil), found through pkg-config
or as a sibling checkout.

```bash
make            # shared and static libraries
make test       # unit tests
sudo make install
```

| Target | What it does |
| --- | --- |
| `make test` | Run the unit tests |
| `make test-quiet` | One line per suite |
| `make test-valgrind-quiet` | Same, under Valgrind |
| `make test-asan` | Rebuild with ASan+UBSan and run the suite |
| `make coverage` | Line coverage, per file |
| `make fuzz` | Build and run both fuzzers (`FUZZ_TIME=3600` for a real campaign) |
| `make docs` | Doxygen, into `./docs` |

## The dialect

What the parsers accept, what they record, and what a consumer has to check
afterwards is specified in
[documentation/obj-mtl.md](documentation/obj-mtl.md). Where the
implementation departs from that document, the document says so.

## The API

Everything is prefixed `gmdl_` / `GMDL_`, under `<ghoti.io/model/...>`.

- **`core.h`** - `GMDL_Result`, `gmdl_result_string()`, and `GMDL_Limits`.
- **`stream.h`** - `GMDL_Stream` over memory or a file, with a line reader.
- **`obj.h`** - `gmdl_obj_load()`, `gmdl_obj_load_file()`, `gmdl_obj_free()`,
  `gmdl_obj_dump()`.
- **`mtl.h`** - `gmdl_mtl_load()`, `gmdl_mtl_load_file()`, `gmdl_mtl_free()`,
  `gmdl_mtl_find()`, `gmdl_mtl_dump()`.
- **`allocator.h`** - `GMDL_Allocator`, which is cutil's `GCU_Allocator`, so an
  allocator written for any library in the suite works with all of them.

Every load function takes a `GMDL_Limits *` and a `GMDL_Allocator *`, both of
which may be NULL for the defaults.

### Limits

The only limit with a value by default is `max_line_length` (64 KiB): the
parser reads a line at a time, so without it a single unterminated line would
be read into memory in its entirety. The record caps default to "no limit",
because the size of the input already bounds them - every record costs at least
a couple of bytes - and a legitimate model can be very large. Set them when the
input is untrusted and you would rather fail early:

```c
GMDL_Limits limits;
gmdl_limits_default(&limits);
limits.max_vertices = 1u << 20;
limits.max_faces = 1u << 20;
```

### A line that does not fit is an error

`gmdl_stream_read_line()` reports `GMDL_ERR_LIMIT` for a line longer than the
buffer rather than handing back a prefix. A fixed `fgets()` buffer silently
splits such a line, and the remainder is then parsed as though it were a line
of its own - a face line cut in half becomes a second, shorter face.

## Status

80 tests, clean under Valgrind and under ASan+UBSan, 87.6% line coverage. Both
fuzzers run clean: OBJ 2.1M executions, MTL 4.1M.

The uncovered lines are almost entirely allocation-failure branches, which
need fault injection to reach.
