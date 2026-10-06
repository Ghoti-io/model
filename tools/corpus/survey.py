#!/usr/bin/env python3
"""Fail if a pinned 3ds Max MTL writes a map spelling the loader does not read.

The files are fetched by tools/corpus/fetch.sh from the commits in
tools/corpus/pins. They are not in the repository. Each one opens with the
Guruware banner, which is how these particular pins are known to be that
exporter.

The spellings the loader reads are the gmdl_line_is(line_text, "...")
literals in src/mtl/mtl_load.c. A second hand-kept set would stay green
after the loader dropped one, which is the miss this gate is for.
"""

import argparse
import hashlib
import pathlib
import re
import sys

ROOT = pathlib.Path(__file__).resolve().parents[2]
PINS = ROOT / "tools" / "corpus" / "pins"
CORPUS = ROOT / "third_party" / "mtl-exporters"
LOADER = ROOT / "src" / "mtl" / "mtl_load.c"

BANNER = "# 3ds Max Wavefront OBJ Exporter v0.97b - (c)2007 guruware"

# Bare names the loader uses for a map, plus near-misses a file can write
# that do not begin with "map_". A near-miss is reported when the loader
# does not have that exact spelling. This is not every synonym.
BARE = {"bump", "norm", "disp", "decal", "refl"}
NEAR = {"normal", "bumpmap", "displacement", "reflection"}

LINE_IS = re.compile(r'gmdl_line_is\(\s*line_text\s*,\s*"([^"]+)"')


def load_pins():
    rows = []
    for raw in PINS.read_text(encoding="utf-8").splitlines():
        line = raw.strip()
        if not line or line.startswith("#"):
            continue
        name, digest, url = line.split()
        rows.append((name, digest, url))
    if not rows:
        raise SystemExit("%s names no files" % PINS)
    return rows


def pin_digest(name):
    for pinned, digest, _url in load_pins():
        if pinned == name:
            return digest
    raise SystemExit("%s is not a name in %s" % (name, PINS))


def file_digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def digest_matches(name, path):
    return file_digest(pathlib.Path(path)) == pin_digest(name)


def loader_map_spellings():
    """Map directives mtl_load.c matches as a whole line, exact spelling."""
    text = LOADER.read_text(encoding="utf-8")
    found = []
    for token in LINE_IS.findall(text):
        if is_map_token(token) and token not in found:
            found.append(token)
    if not found:
        raise SystemExit("%s: no map directive literals" % LOADER)
    return found


def is_map_token(token):
    folded = token.casefold()
    return (
        folded.startswith("map_")
        or folded in BARE
        or folded in NEAR
    )


def map_token(line):
    """The map directive on this line, or None when the line is not one."""
    text = line.strip()
    if not text or text.startswith("#"):
        return None
    token = text.split()[0]
    if is_map_token(token):
        return token
    return None


def survey_text(name, text, accepted):
    """Return a list of problems. Empty means the text is in the accepted set."""
    problems = []
    lines = text.splitlines()
    if not lines or lines[0].rstrip("\r") != BANNER:
        problems.append("%s: first line is not the 3ds Max exporter banner" % name)
    seen = []
    for lineno, line in enumerate(lines, 1):
        token = map_token(line)
        if token is None:
            continue
        seen.append(token)
        if token not in accepted:
            problems.append(
                "%s:%d: map spelling %r is not one mtl_load.c reads"
                % (name, lineno, token)
            )
    if not seen and not any("banner" in item for item in problems):
        problems.append("%s: no map directive, so the pin surveys nothing" % name)
    return problems


def survey_corpus():
    accepted = loader_map_spellings()
    problems = []
    for name, digest, _url in load_pins():
        path = CORPUS / ("%s.mtl" % name)
        if not path.is_file():
            problems.append("%s is missing; run tools/corpus/fetch.sh" % path)
            continue
        if file_digest(path) != digest:
            problems.append(
                "%s is not the pinned bytes; run tools/corpus/fetch.sh" % path
            )
            continue
        problems.extend(
            survey_text(name, path.read_text(encoding="utf-8"), accepted)
        )
    return problems


def self_test():
    """Planted spellings the corpus does not contain, and the loader's own list."""
    accepted = loader_map_spellings()
    problems = []
    for required in ("map_bump", "bump", "map_Bump"):
        if required not in accepted:
            problems.append("self-test: mtl_load.c does not read %s" % required)
    ok = survey_text("planted-ok", BANNER + "\n map_bump nrm.png\n", accepted)
    bad = survey_text("planted-bad", BANNER + "\n\tmap_Kn nrm.png\n", accepted)
    folded = survey_text("planted-case", BANNER + "\nMap_Bump nrm.png\n", accepted)
    near = survey_text("planted-near", BANNER + "\nnormal nrm.png\n", accepted)
    empty = survey_text("planted-empty", BANNER + "\nKd 1 1 1\n", accepted)
    nobanner = survey_text("planted-banner", "map_Kn a.png\n", accepted)
    if ok:
        problems.append("self-test: map_bump was refused: %s" % ok)
    if not any("map_Kn" in item for item in bad):
        problems.append("self-test: map_Kn was not named in %s" % bad)
    if not any("Map_Bump" in item for item in folded):
        problems.append("self-test: Map_Bump was not named in %s" % folded)
    if not any("normal" in item for item in near):
        problems.append("self-test: normal was not named in %s" % near)
    if not any("no map directive" in item for item in empty):
        problems.append("self-test: a banner with no map was accepted: %s" % empty)
    if not any("banner" in item for item in nobanner):
        problems.append("self-test: a file with no banner was accepted")
    if not any("map_Kn" in item for item in nobanner):
        problems.append("self-test: a bad banner hid the map line: %s" % nobanner)
    return problems


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--self-test",
        action="store_true",
        help="check the classifier against planted spellings",
    )
    parser.add_argument(
        "--matches",
        nargs=2,
        metavar=("NAME", "PATH"),
        help="exit 0 when PATH is the pinned bytes for NAME",
    )
    args = parser.parse_args()
    if args.matches:
        name, path = args.matches
        return 0 if digest_matches(name, path) else 1
    problems = self_test() if args.self_test else survey_corpus()
    if problems:
        for problem in problems:
            print(problem, file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
