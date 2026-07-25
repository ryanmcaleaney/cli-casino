#!/bin/sh
# Install Casino CLI on Linux.  Run with sudo for the default prefix.
set -eu

PREFIX=/usr/local
DESTDIR=${DESTDIR:-}
PROGRAMS='roulette coin dice sicbo baccarat blackjack craps slots videopoker war threecard chuckaluck bigsix'

usage() {
    cat <<'EOF'
Usage: ./install.sh [--prefix PREFIX]

Build and install casino to PREFIX/bin (default: /usr/local/bin), then create
game-name symlinks there.  Use sudo for the default prefix, or install for the
current user with:

  ./install.sh --prefix "$HOME/.local"

For package staging, set DESTDIR; for example:

  DESTDIR=/tmp/package-root ./install.sh --prefix /usr
EOF
}

while [ "$#" -gt 0 ]; do
    case $1 in
        --prefix)
            [ "$#" -ge 2 ] || { echo 'install.sh: --prefix needs a value' >&2; exit 2; }
            PREFIX=$2
            shift 2
            ;;
        --help|-h)
            usage
            exit 0
            ;;
        *)
            echo "install.sh: unknown option: $1" >&2
            usage >&2
            exit 2
            ;;
    esac
done

case $PREFIX in
    /*) ;;
    *) echo 'install.sh: prefix must be an absolute path' >&2; exit 2 ;;
esac

command -v make >/dev/null 2>&1 || {
    echo 'install.sh: make is required' >&2
    exit 1
}
command -v install >/dev/null 2>&1 || {
    echo 'install.sh: install is required' >&2
    exit 1
}

make

bindir=${DESTDIR}${PREFIX}/bin
install -d -m 755 "$bindir"
install -m 755 casino "$bindir/casino"

for program in $PROGRAMS; do
    ln -sfn casino "$bindir/$program"
done

echo "Installed casino and game symlinks to $bindir"
