#!/usr/bin/env bash
#
# uninstall.sh - removes a musikoden install created by install.sh, pacman-style.
#
# Generated from Templates/scripts/uninstall.sh.template (generic form of
# purplebee/uninstall.sh). Keep the values in sync with install.sh: both
# scripts derive paths from the same settings. Same style as the installer:
#   - purple pacman-style output (NO_COLOR / FORCE_COLOR aware)
#   - --dry-run, --prefix / --bindir overrides, ~ expansion
#   - user config preserved unless --purge-data is given
#   - only removes files it knows the installer created; prunes empty dirs
#
# Layout (default prefix: ~/.local):
#
#   ~/.local/bin/musikoden                        launcher
#   ~/.local/lib/musikoden/musikoden              real binary
#   ~/.config/musikoden/                          user config + log (kept)
#   ~/.local/share/applications/musikoden.desktop
#   ~/.local/share/icons/hicolor/256x256/apps/musikoden.ico
#
set -euo pipefail

# ===========================================================================
# PROJECT CONFIGURATION - edit these for each application
# ===========================================================================

APP="musikoden"                      # id: binary name, .desktop file, icon name
ICON_EXT="ico"                       # installed icon extension
ICON_THEME_REL="icons/hicolor/256x256/apps"  # hicolor bucket, relative to share/
LEGACY_ICON_RELS="icons/hicolor/256x256/apps/musikoden.png icons/hicolor/300x300/apps/musikoden.png"  # "" = none

DATA_SUBDIR=".config/musikoden"      # user config under $HOME ("" = no data dir)
OLD_DATA_SUBDIR=""                   # old location under lib/<APP>/ ("" = none)

# ===========================================================================
# END OF PROJECT CONFIGURATION - generic code starts here
# ===========================================================================

PREFIX="${HOME}/.local"
BIN_DIR=""
PURGE_DATA=0
DRY_RUN=0
FOUND=0

# Same purple, pacman-style output as install.sh.
if [ -n "${FORCE_COLOR:-}" ] || { [ -t 1 ] && [ -z "${NO_COLOR:-}" ] && [ "${TERM:-dumb}" != "dumb" ]; }; then
    C_PURPLE=$'\033[1;35m'
    C_RED=$'\033[1;31m'
    C_YELLOW=$'\033[1;33m'
    C_RESET=$'\033[0m'
else
    C_PURPLE= C_RED= C_YELLOW= C_RESET=
fi

die()  { printf '%s::%s %serror:%s %s\n' "$C_PURPLE" "$C_RESET" "$C_RED" "$C_RESET" "$*" >&2; exit 1; }
step() { printf '%s::%s %s\n' "$C_PURPLE" "$C_RESET" "$*"; }
note() { printf '   %s\n' "$*"; }
warn() { printf '%s::%s %swarning:%s %s\n' "$C_PURPLE" "$C_RESET" "$C_YELLOW" "$C_RESET" "$*" >&2; }

usage() {
    cat <<EOF
Usage: $0 [options]

Removes the launcher, binary, desktop entry and icon created by install.sh.

Options:
  --prefix DIR    Install root to uninstall from (default: \$HOME/.local).
  --prefix=DIR    Same, with an explicit '='.
  --bindir DIR    Launcher directory (default: PREFIX/bin).
  --user          Uninstall from \$HOME/.local (the default).
  --purge-data    Also delete ~/$DATA_SUBDIR. Off by default:
                  user data is always kept unless this flag is given.
  --dry-run       Print what would be removed without changing anything.
  -h, --help      Show this help.

Examples:
  $0                             # remove app, keep user data
  $0 --prefix /opt/$APP          # custom prefix
  $0 --purge-data                # remove app and user data
  $0 --dry-run --purge-data

Environment:
  NO_COLOR=1     Disable colored output.
  FORCE_COLOR=1  Force colored output (e.g. when piping to a log).

Note: this script only removes files it knows install.sh created. It does not
edit your PATH or remove unrelated files in the install directories.
EOF
}

while [ "$#" -gt 0 ]; do
    case "$1" in
        --prefix)
            [ "$#" -ge 2 ] || die "--prefix requires a directory"
            PREFIX="$2"; shift 2 ;;
        --prefix=*)
            PREFIX="${1#*=}"; shift ;;
        --bindir)
            [ "$#" -ge 2 ] || die "--bindir requires a directory"
            BIN_DIR="$2"; shift 2 ;;
        --bindir=*)
            BIN_DIR="${1#*=}"; shift ;;
        --user)
            PREFIX="${HOME}/.local"; shift ;;
        --purge-data)
            PURGE_DATA=1; shift ;;
        --dry-run)
            DRY_RUN=1; shift ;;
        -h|--help)
            usage; exit 0 ;;
        --)
            shift; break ;;
        *)
            die "unknown option: $1 (try --help)" ;;
    esac
done

[ "$#" -eq 0 ] || die "unexpected argument: $1"

# Expand a leading ~ and make relative paths absolute, exactly like install.sh.
case "$PREFIX" in
    "~"|"~/"*) PREFIX="$HOME${PREFIX#\~}" ;;
esac
case "$PREFIX" in
    /*) ;;
    *) PREFIX="$PWD/$PREFIX" ;;
esac
case "$BIN_DIR" in
    "" ) BIN_DIR="$PREFIX/bin" ;;
    "~"|"~/"*) BIN_DIR="$HOME${BIN_DIR#\~}" ;;
esac
case "$BIN_DIR" in
    /*) ;;
    *) BIN_DIR="$PWD/$BIN_DIR" ;;
esac

[ "$PREFIX" != "/" ] || die "refusing to uninstall from '/' (pass --prefix)"

LIB_DIR="$PREFIX/lib/$APP"
REAL_BIN="$LIB_DIR/$APP"
WRAPPER="$BIN_DIR/$APP"
SHARE_DIR="$PREFIX/share"
DESKTOP_FILE="$SHARE_DIR/applications/$APP.desktop"
ICON_DIR="$SHARE_DIR/$ICON_THEME_REL"
ICON_FILE="$ICON_DIR/$APP.$ICON_EXT"
if [ -n "$DATA_SUBDIR" ]; then
    DATA_DIR="${HOME%/}/$DATA_SUBDIR"
else
    DATA_DIR=""
fi
if [ -n "$OLD_DATA_SUBDIR" ]; then
    OLD_DATA_DIR="$LIB_DIR/$OLD_DATA_SUBDIR"
else
    OLD_DATA_DIR=""
fi

if [ "$DRY_RUN" -eq 1 ]; then
    step "Dry run: nothing will be changed"
fi
step "Uninstalling $APP from $PREFIX"

remove_file() {
    local target="$1"
    [ -e "$target" ] || [ -L "$target" ] || return 0
    FOUND=1
    if [ "$DRY_RUN" -eq 1 ]; then
        note "would remove $target"
    else
        rm -f -- "$target" || die "could not remove $target"
        note "Removed $target"
    fi
}

remove_tree() {
    local target="$1"
    [ -d "$target" ] || return 0
    FOUND=1
    if [ "$DRY_RUN" -eq 1 ]; then
        note "would remove $target (and its contents)"
    else
        rm -rf -- "$target" || die "could not remove $target"
        note "Removed $target"
    fi
}

prune_dir() {
    local target="$1"
    [ -d "$target" ] || return 0
    if [ "$DRY_RUN" -eq 1 ]; then
        note "would remove empty directory $target"
    else
        rmdir -- "$target" 2>/dev/null || true
    fi
}

remove_file "$WRAPPER"
remove_file "$REAL_BIN"
remove_file "$DESKTOP_FILE"
remove_file "$ICON_FILE"

# Older installs used PNG icons; clean those up too.
for rel in $LEGACY_ICON_RELS; do
    remove_file "$SHARE_DIR/$rel"
done

if [ "$PURGE_DATA" -eq 1 ]; then
    [ -z "$DATA_DIR" ] || remove_tree "$DATA_DIR"
    [ -z "$OLD_DATA_DIR" ] || remove_tree "$OLD_DATA_DIR"
elif [ -n "$DATA_DIR" ]; then
    note "Kept user data in $DATA_DIR (use --purge-data to delete it)"
fi

# Only remove directories that are now empty; never touch unrelated files.
prune_dir "$ICON_DIR"
for rel in $LEGACY_ICON_RELS; do
    prune_dir "$(dirname -- "$SHARE_DIR/$rel")"
done
prune_dir "$LIB_DIR"

# Refresh the hicolor cache now that the icon is gone.
if [ "$DRY_RUN" -eq 0 ] && command -v gtk-update-icon-cache >/dev/null 2>&1; then
    gtk-update-icon-cache -f -t "$SHARE_DIR/icons/hicolor" >/dev/null 2>&1 || true
fi

if [ "$FOUND" -eq 0 ]; then
    warn "no $APP installation found under $PREFIX (check --prefix/--bindir)"
fi

if [ "$DRY_RUN" -eq 1 ]; then
    step "Dry run complete"
else
    step "Uninstall complete"
fi
