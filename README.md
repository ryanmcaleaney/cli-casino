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

## Build

Requires a C11 compiler and `make`. The graphical frontends use raylib. The
build prefers a system raylib discovered through `pkg-config`. On Linux it can
fall back to the vendored raylib library in `vendor/raylib`.

On macOS, install the native raylib library first. The vendored library is a
Linux binary and is deliberately not used:

```sh
brew install raylib pkg-config
```

If raylib is missing on macOS, `make` stops with the installation command
instead of trying to link the incompatible Linux library.

```sh
make
./casino --help
```

Run the regression tests with:

```sh
make test
```

To remove generated files:

```sh
make clean
```

## Install on Linux and macOS

The installer builds and installs the game, its command aliases, and all GUI
assets:

```sh
sudo ./install.sh
```

The default layout is:

```text
/usr/local/bin/casino
/usr/local/bin/{roulette,blackjack,videopoker,...}
/usr/local/share/casino/Assets/
```

Assets are data files rather than executables, so they belong under
`share/casino`. The installer builds that location into the installed binary;
the GUI can therefore find its cards, fonts, and sounds regardless of the
directory from which the game is launched.

To install for only the current user, without `sudo`:

```sh
./install.sh --prefix "$HOME/.local"
```

Make sure `$HOME/.local/bin` is on `PATH`, then run `casino`, `blackjack`,
`videopoker`, or another installed game name directly.

To use another location:

```sh
./install.sh --prefix /opt/casino
```

For distribution-package staging, use `DESTDIR`. `DESTDIR` changes only the
temporary staging root; the final runtime asset path is still based on
`--prefix`:

```sh
DESTDIR=/tmp/package-root ./install.sh --prefix /usr
```

## Usage

Games can be selected as the first argument to `casino`:

```sh
./casino --help
./casino roulette --seed 42 red straight:17
./casino roulette --seed 7 --iterations 1000 --stats red
./casino coin --seed 1 heads
./casino dice --seed 1 2d6 total:7
./casino baccarat --seed 1 player
./casino blackjack --seed 1 hit stand
./casino war --seed 17 war
./casino threecard --seed 1 ante:25 pairplus:5 play
./casino letitride --seed 3 bet:25 ride,ride
./casino caribbeanstud --seed 7 ante:25 raise
```

After running `make symlinks` or installing the project, each game can also be
called by name:

```sh
roulette --seed 42 red
blackjack hit stand
```

Useful options:

- `--seed N` makes a run reproducible.
- `--iterations N` plays multiple rounds.
- `--runs N` is shorthand for `--iterations N --stats`.
- `--stats` prints a summary instead of each round.
- `--quiet` prints compact output.
- `--json` emits newline-delimited JSON.
- `--list-bets` shows supported bets or actions for a game.

`blackjack` accepts scripted actions (`hit`, `stand`, or `double`), or runs
interactively when no actions are supplied. It uses a six-deck shoe with the
dealer standing on soft 17.

`threecard` deals three cards each from a single deck. After seeing the hand
the player takes `play` (a second wager equal to the ante) or `fold`; the
dealer qualifies with queen-high or better. A straight beats a flush with
only three cards. An ante bonus rides on any played hand and the optional
`pairplus:N` side bet pays on the player's own cards even after a fold.
Simulation with `--runs` requires a scripted `play` or `fold`.

`letitride` deals three cards to the player and two community cards from a
single deck. Three equal wagers go up at the start (`bet:25` commits 75);
the player may pull bet 1 after seeing three cards and bet 2 after the
first community card, while bet 3 always rides. Every wager still riding
is paid separately from a tens-or-better pay table. A pulled wager is
returned and never counts as gambled. Simulation with `--runs` requires
scripted decisions such as `ride,ride` or `pull,ride`.

`caribbeanstud` deals five cards each from a single deck, with one dealer
card face up. After seeing the hand the player takes `raise` (a second wager
of exactly twice the ante) or `fold`; the dealer qualifies with ace-king high
or better. A non-qualifying dealer pays the ante 1:1 and pushes the raise,
while a winning raise is paid from a pay table running from 1:1 for a high
card to 100:1 for a royal flush. The progressive jackpot side bet is not
included. Simulation with `--runs` plays Caribbean Stud basic strategy unless
a `raise` or `fold` is scripted.

`war` deals one card each from a six-deck shoe, ace high. A tie is resolved
with `war` (match the wager, burn three cards, one more card each) or
`surrender` (lose half the wager); supplying neither prompts interactively.
Simulation with `--runs` requires one of the two scripted tie strategies.

## Graphical games

Raylib builds provide graphical frontends for video poker, baccarat,
blackjack, Ride the Bus, three card poker, Let It Ride, and Caribbean
Stud:

```sh
casino videopoker --gui
casino baccarat --gui
casino blackjack --gui
casino ridethebus --gui
casino threecard --gui
casino letitride --gui
casino caribbeanstud --gui
```

Video poker also has a terminal trainer and a graphical optimal-play mode.
Blackjack has a graphical Hi-Lo counting trainer:

```sh
casino videopoker --trainer
casino videopoker --gui --optimal
casino blackjack --gui --counting
```

## Implemented and planned games

Implemented:

- European roulette
- Coin flip
- Generic NdM dice
- Punto Banco baccarat
- Six-deck blackjack
- Pass-line craps
- Three-reel slots
- Jacks or Better video poker
- Ride the Bus
- Casino War
- Three Card Poker
- Let It Ride

Sic bo, chuck-a-luck, and big six are currently placeholders and report
that they are not implemented.

## Project layout

- `src/main.c` — game selection and program entry
- `src/cli.c` — shared command-line parsing
- `src/games/` — game implementations
- `src/gui/` — shared raylib UI and graphical game frontends
- `Assets/` — playing cards, fonts, sounds, and their licences
- `vendor/raylib/` — vendored raylib headers and library
- `install.sh` — Linux installation and asset deployment
- `tests/run.sh` — end-to-end regression tests
