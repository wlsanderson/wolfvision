#!/usr/bin/env -S uv run --locked
"""Check C++/Python formatting and lint; --fix applies formatting and safe Ruff fixes."""

import argparse
import json
import pathlib
import shutil
import subprocess
import sys

ROOT = pathlib.Path(__file__).resolve().parents[1]


def run(*arguments):
    print("+ " + " ".join(map(str, arguments)), flush=True)
    subprocess.run(list(map(str, arguments)), cwd=ROOT, check=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--fix", action="store_true", help="format code and apply safe Python lint fixes"
    )
    parser.add_argument(
        "--build", default="build", help="host CMake build directory (default: build)"
    )
    options = parser.parse_args()
    for tool in ("clang-format", "clang-tidy", "ruff"):
        if not shutil.which(tool):
            parser.error(
                f"Missing {tool}; run uv sync --locked, then invoke this script through uv run --locked."
            )

    build = (ROOT / options.build).resolve()
    database = build / "compile_commands.json"
    if not database.is_file():
        parser.error(
            "Missing compile_commands.json; configure a host build with cmake -S . -B build first, "
            "or select an existing build with --build PATH."
        )

    cpp = sorted(
        path for directory in ("src", "tests") for path in (ROOT / directory).rglob("*.cpp")
    )
    headers = sorted(
        path for directory in ("src", "tests") for path in (ROOT / directory).rglob("*.hpp")
    )
    # Refuse stale/incomplete databases instead of silently skipping a new source or test.
    entries = json.loads(database.read_text())
    compiled = {(pathlib.Path(entry["directory"]) / entry["file"]).resolve() for entry in entries}
    missing = set(cpp) - compiled
    if missing:
        parser.error(
            "Reconfigure the host build with BUILD_TESTING=ON; compilation commands are missing for "
            + ", ".join(str(path.relative_to(ROOT)) for path in sorted(missing))
        )

    files = [str(path.relative_to(ROOT)) for path in sorted(cpp + headers)]
    if options.fix:
        run("clang-format", "-i", *files)
        run("ruff", "check", "--fix", ".")
        run("ruff", "format", ".")

    run("clang-format", "--dry-run", "--Werror", *files)
    run("ruff", "check", ".")
    run("ruff", "format", "--check", ".")
    # Headers are checked through their translation units, using the project header filter.
    for path in cpp:
        run("clang-tidy", "--quiet", "-p", build, path.relative_to(ROOT))
    print("PASS: C++ and Python formatting/lint checks.")


if __name__ == "__main__":
    try:
        main()
    except subprocess.CalledProcessError as error:
        sys.exit(error.returncode)
