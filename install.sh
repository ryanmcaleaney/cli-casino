#!/bin/sh
# Install Casino on Linux or macOS. Run with sudo for the default prefix.
set -eu

PREFIX=/usr/local
DESTDIR=${DESTDIR:-}
PROGRAMS='roulette coin dice sicbo baccarat blackjack craps slots videopoker ridethebus war threecard chuckaluck bigsix'
ASSET_DIR=Assets

usage() {
    cat <<'EOF'
Usage: ./install.sh [--prefix PREFIX]

Build and install casino to PREFIX/bin (default: /usr/local/bin), copy GUI
assets to PREFIX/share/casino/Assets, then create game-name symlinks.  Use
sudo for the default prefix, or install for the current user with:

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

[ -d "$ASSET_DIR" ] || {
    echo "install.sh: required asset directory not found: $ASSET_DIR" >&2
    exit 1
}

# Force a rebuild because the GUI's compiled-in data path changes for an
# installed copy.  DESTDIR is deliberately excluded: it is only a staging
# root and must not become part of the runtime path.
assetdir=${PREFIX}/share/casino/Assets
make -B ASSET_ROOT="$assetdir"

bindir=${DESTDIR}${PREFIX}/bin
datadir=${DESTDIR}${assetdir}
install -d -m 755 "$bindir"
install -m 755 casino "$bindir/casino"
install -d -m 755 "$datadir"
cp -R "$ASSET_DIR"/. "$datadir"/

for program in $PROGRAMS; do
    ln -sfn casino "$bindir/$program"
done

echo "Installed casino and game symlinks to $bindir"
echo "Installed GUI assets to $datadir"
