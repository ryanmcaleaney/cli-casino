# Casino CLI

C11 command-line casino simulator.

## Commands

- Build: `make`
- Test: `make test`
- Clean: `make clean`

## Layout

- `src/main.c`: program entry and game dispatch
- `src/cli.c`: shared command-line option parsing
- `src/games/`: per-game implementations
- `tests/run.sh`: end-to-end regression tests

## Working rules

- Keep C11 compatibility and the existing CLI/output formats.
- For a game change, read only its source/header, relevant shared code, and the matching test section.
- Add or update a regression test for behavior changes.
- Run `make test` after code changes.
- Do not inspect generated files in `build/`, the `casino` executable, or game symlinks.
