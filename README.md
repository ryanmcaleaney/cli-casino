# Casino CLI

A small C11 command-line casino-game simulator. It is intended for local
simulation and testing only; it has no money handling, accounts, networking,
or connection to real gambling services.

## AI-generation disclosure

This repository is an AI-generation experiment: it was created as a
**Claude Fable 5 test**. The code, tests, and documentation in this project
were AI-generated at the direction of the repository owner. They may contain
mistakes, incomplete rules, security issues, or inaccurate gambling logic.

Do not treat this project as a production system, an authoritative statement
of casino rules or odds, or software suitable for real-money gambling. Review,
test, and validate any material before relying on it.

## Build and test

Requires a C11 compiler and `make`.

```sh
make
make test
```

To remove generated files:

```sh
make clean
```

## Install on Linux

The included installer builds the program, installs `casino`, creates
game-name symlinks in `/usr/local/bin`, and installs the GUI assets under
`/usr/local/share/casino/Assets`.  The installed GUI uses that data directory,
so it works from any current directory:

```sh
sudo ./install.sh
```

To install without elevated privileges:

```sh
./install.sh --prefix "$HOME/.local"
```

For distribution-package staging, use `DESTDIR` (for example,
`DESTDIR=/tmp/package-root ./install.sh --prefix /usr`).

## Usage

```sh
./casino --help
./casino roulette --seed 42 red straight:17
./casino roulette --seed 7 --iterations 1000 --stats red
./casino coin --seed 1 heads
./casino dice --seed 1 2d6 total:7
./casino baccarat --seed 1 player
./casino blackjack --seed 1 hit stand
```

Useful options:

- `--seed N` makes a run reproducible.
- `--iterations N` plays multiple rounds.
- `--stats` prints a summary instead of each round.
- `--quiet` prints compact output.
- `--json` emits newline-delimited JSON.
- `--list-bets` shows supported bets or actions for a game.

`blackjack` accepts scripted actions (`hit`, `stand`, or `double`), or runs
interactively when no actions are supplied. The implementation uses one deck
and dealer-stands-on-soft-17 rules.

## Implemented and planned games

Implemented: roulette, coin flip, generic dice, Punto Banco baccarat, and
single-deck blackjack.

The command list also includes placeholder entries for sic bo, craps, slots,
video poker, casino war, three-card poker, chuck-a-luck, and big six. Those
entries deliberately report that they are not implemented.

## Project layout

- `src/main.c` — game selection and program entry
- `src/cli.c` — shared command-line parsing
- `src/games/` — game implementations
- `tests/run.sh` — end-to-end regression tests
