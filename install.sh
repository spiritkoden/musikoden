#!/usr/bin/env bash
#
# install.sh - builds and installs musikoden into ~/.local, pacman-style.
#
# Generated from Templates/scripts/install.sh.template (generic form of
# purplebee/install.sh). The same style:
#   - pacman-style output with purple accents (NO_COLOR / FORCE_COLOR aware)
#   - one (n/6) [########] 100% line per completed step
#   - quiet build (full log printed only on failure), --verbose to override
#   - ~/.local by default, --prefix / --bindir overrides, ~ expansion
#   - launcher in bin/, real binary in lib/<app>/, .desktop + hicolor icon
#   - user config in ~/.config/musikoden (created/preserved by default)
#   - built-in --uninstall (with --purge-data) that preserves user config
#
# Layout (default prefix: ~/.local):
#
#   ~/.local/bin/musikoden                        launcher (put this on PATH)
#   ~/.local/lib/musikoden/musikoden              real binary
#   ~/.config/musikoden/                          user config + log (preserved)
#   ~/.local/share/applications/musikoden.desktop
#   ~/.local/share/icons/hicolor/256x256/apps/musikoden.png
#
# The launcher uses `exec` so /proc/self/exe still points at the real binary.
#
set -euo pipefail

# ===========================================================================
# PROJECT CONFIGURATION - edit these for each application
# ===========================================================================

APP="musikoden"                      # id: binary name, .desktop file, icon name
APP_NAME="Musikoden"                 # Name= in the .desktop file
APP_GENERIC_NAME="Music Player"      # GenericName= (use "" to omit the line)
APP_COMMENT="Minimal SDL3 music player with waveform visualizer"  # Comment= ("" to omit)
APP_CATEGORIES="AudioVideo;Audio;Player;"  # Categories= (semicolon separated)

SRC_BIN_REL="musikoden"              # binary produced by the build, relative to script dir
ICON_REL="assets/icon.png"           # icon source, relative to script dir ("" = no icon)
ICON_EXT="png"                       # installed icon extension
ICON_THEME_REL="icons/hicolor/256x256/apps"  # hicolor bucket, relative to share/
LEGACY_ICON_RELS="icons/hicolor/300x300/apps/musikoden.png"  # "" = none

DATA_SUBDIR=".config/musikoden"      # user config under $HOME ("" = no data dir)
OLD_DATA_SUBDIR=""                   # old location under lib/<APP>/ ("" = none)

BUILD_TOOLS="cc make pkg-config"     # required commands ("" = none)
PKG_MODULES="sdl3 sdl3-ttf faad2 fontconfig"  # required pkg-config modules ("" = none)
# Build command, evaluated from the script directory. Use ':' if there is no
# build step (then also use --no-build to install a prebuilt $SRC_BIN_REL).
BUILD_CMD='{ make clean >/dev/null 2>&1 || true; make; }'

# Distro-specific hints printed when a dependency is missing. Edit per project.
print_dep_hints() {
    cat >&2 <<'HINTS'
  Debian/Ubuntu: sudo apt install build-essential pkg-config libsdl3-dev libsdl3-ttf-dev libfaad-dev libfontconfig1-dev
  Fedora/RHEL:   sudo dnf install gcc make pkgconf-pkg-config SDL3-devel SDL3_ttf-devel faad2-devel fontconfig-devel
  Arch:          sudo pacman -S --needed base-devel pkgconf sdl3 sdl3-ttf faad2 fontconfig
HINTS
}

# ===========================================================================
# END OF PROJECT CONFIGURATION - generic code starts here
# ===========================================================================

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
SRC_BIN="$SCRIPT_DIR/$SRC_BIN_REL"
ICON_SRC="$SCRIPT_DIR/$ICON_REL"

PREFIX="${HOME}/.local"
BIN_DIR=""
UNINSTALL=0
PURGE_DATA=0
SKIP_BUILD=0
VERBOSE=0

# Colors are enabled on a TTY unless NO_COLOR is set or TERM=dumb;
# FORCE_COLOR=1 overrides detection.
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

# Print a pacman-like completed step: (1/6) label [########] 100%.
progress() {
    local current="$1" total="$2" label="$3"
    local width=20
    local i bar=""
    for (( i = 0; i < width; i++ )); do bar+='#'; done
    printf '(%d/%d) %-44s [%s%s%s] 100%%\n' \
        "$current" "$total" "$label" "$C_PURPLE" "$bar" "$C_RESET"
}

usage() {
    cat <<EOF
Usage: $0 [options]

Builds $APP from source and installs it into your home directory.

Options:
  --prefix DIR    Install root (default: \$HOME/.local).
                  Use e.g. --prefix /usr/local for a system-wide install.
  --bindir DIR    Override the launcher directory (default: PREFIX/bin).
  --user          Install into \$HOME/.local (the default).
  --no-build      Skip compilation and install the existing ./$SRC_BIN_REL binary.
  --verbose, -v   Show build output (hidden by default; failures still print it).
  --uninstall     Remove the launcher, binary, desktop entry and icon.
  --purge-data    With --uninstall, also delete ~/$DATA_SUBDIR.
                  Without this flag user data is always preserved.
  -h, --help      Show this help.

Examples:
  $0                        # install to ~/.local
  $0 --prefix /opt/$APP     # custom prefix
  $0 --uninstall            # remove the app, keep user data
  $0 --uninstall --purge-data

Environment:
  NO_COLOR=1     Disable colored output.
  FORCE_COLOR=1  Force colored output (e.g. when piping to a log).

Note: add \$PREFIX/bin to your PATH if it is not there already:
  export PATH="\$HOME/.local/bin:\$PATH"
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
        --no-build)
            SKIP_BUILD=1; shift ;;
        --verbose|-v)
            VERBOSE=1; shift ;;
        --uninstall)
            UNINSTALL=1; shift ;;
        --purge-data)
            PURGE_DATA=1; shift ;;
        -h|--help)
            usage; exit 0 ;;
        --)
            shift; break ;;
        *)
            die "unknown option: $1 (try --help)" ;;
    esac
done

[ "$#" -eq 0 ] || die "unexpected argument: $1"

# Expand a leading ~ and make relative prefixes absolute.
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

check_dependencies() {
    local tool missing=""
    step "Checking build dependencies"
    for tool in $BUILD_TOOLS; do
        command -v "$tool" >/dev/null 2>&1 || die \
            "'$tool' is required to build $APP (install your build tools first)"
    done
    if [ -n "$PKG_MODULES" ]; then
        for tool in $PKG_MODULES; do
            pkg-config --exists "$tool" || missing="$missing $tool"
        done
    fi
    if [ -n "$missing" ]; then
        printf 'missing development packages:%s\n' "$missing" >&2
        print_dep_hints
        die "unmet build dependencies"
    fi
}

build() {
    step "Building $APP"

    # Keep the build quiet on success; show the full log only when it fails.
    local log=""
    [ "$VERBOSE" -eq 1 ] || log="$(mktemp)" || die "could not create build log"

    if [ "$VERBOSE" -eq 1 ]; then
        ( cd "$SCRIPT_DIR" && eval "$BUILD_CMD" ) || die "build failed"
    else
        if ! ( cd "$SCRIPT_DIR" && eval "$BUILD_CMD" ) >"$log" 2>&1; then
            cat "$log" >&2
            rm -f "$log"
            die "build failed (see output above)"
        fi
        rm -f "$log"
    fi

    [ -x "$SRC_BIN" ] || die "expected binary '$SRC_BIN' was not produced"
    progress 1 1 "Compiling and linking"
}

handle_data() {
    [ -n "$DATA_DIR" ] || { note "No user data directory configured"; return 0; }

    [ ! -e "$DATA_DIR" ] || [ -d "$DATA_DIR" ] \
        || die "$DATA_DIR exists but is not a directory"

    local data_action
    if [ -n "$OLD_DATA_DIR" ] && [ -d "$OLD_DATA_DIR" ] \
        && [ "$OLD_DATA_DIR" != "$DATA_DIR" ]; then
        if [ ! -e "$DATA_DIR" ] \
            || { [ -d "$DATA_DIR" ] && [ -z "$(ls -A "$DATA_DIR" 2>/dev/null || true)" ]; }; then
            rmdir "$DATA_DIR" 2>/dev/null || true
            mv "$OLD_DATA_DIR" "$DATA_DIR" \
                || die "could not migrate user data to $DATA_DIR"
            data_action="Migrated user data to $DATA_DIR"
        else
            data_action="Keeping existing user data in $DATA_DIR"
            note "Old data directory was left in place: $OLD_DATA_DIR"
        fi
    elif [ -d "$DATA_DIR" ]; then
        data_action="Keeping existing user data"
    else
        install -d -m 755 "$DATA_DIR" || die "could not create $DATA_DIR"
        data_action="Created user data directory"
    fi
    progress 3 6 "$data_action"
}

install_app() {
    [ -z "$ICON_REL" ] || [ -f "$ICON_SRC" ] \
        || die "icon asset '$ICON_SRC' is missing"

    step "Installing $APP to $PREFIX"

    if [ -n "$ICON_REL" ]; then
        install -d -m 755 "$BIN_DIR" "$LIB_DIR" \
            "$SHARE_DIR/applications" "$ICON_DIR" \
            || die "could not create installation directories"
    else
        install -d -m 755 "$BIN_DIR" "$LIB_DIR" \
            "$SHARE_DIR/applications" \
            || die "could not create installation directories"
    fi
    progress 1 6 "Preparing installation directories"

    install -m 755 "$SRC_BIN" "$REAL_BIN" || die "could not install binary"
    progress 2 6 "Installing executable"

    handle_data

    # Escape characters that are special inside a double-quoted shell string.
    local real_escaped
    real_escaped="$(printf '%s' "$REAL_BIN" | sed 's/[\\"$`]/\\&/g')"
    local tmp
    tmp="$(mktemp "${BIN_DIR}/.${APP}.XXXXXX")" || die "could not create launcher"
    cat > "$tmp" <<EOF
#!/bin/sh
# Launcher for $APP. The real binary lives in $LIB_DIR; user data lives in
# ${DATA_DIR:-<none>}.
exec "${real_escaped}" "\$@"
EOF
    chmod 755 "$tmp"
    mv -f "$tmp" "$WRAPPER" || die "could not install launcher"
    progress 4 6 "Creating launcher"

    {
        printf '[Desktop Entry]\n'
        printf 'Type=Application\n'
        printf 'Version=1.0\n'
        printf 'Name=%s\n' "$APP_NAME"
        [ -z "$APP_GENERIC_NAME" ] || printf 'GenericName=%s\n' "$APP_GENERIC_NAME"
        [ -z "$APP_COMMENT" ] || printf 'Comment=%s\n' "$APP_COMMENT"
        printf 'Exec="%s"\n' "$WRAPPER"
        [ -z "$ICON_REL" ] || printf 'Icon=%s\n' "$APP"
        printf 'Terminal=false\n'
        printf 'Categories=%s\n' "$APP_CATEGORIES"
        printf 'StartupNotify=true\n'
    } > "$DESKTOP_FILE" || die "could not install desktop entry"
    progress 5 6 "Installing desktop entry"

    if [ -n "$ICON_REL" ]; then
        install -m 644 "$ICON_SRC" "$ICON_FILE" || die "could not install icon"

        # Drop icons installed by an older layout so they can't shadow this one.
        local rel target parent
        for rel in $LEGACY_ICON_RELS; do
            target="$SHARE_DIR/$rel"
            rm -f "$target" 2>/dev/null || true
            parent="$(dirname -- "$target")"
            rmdir "$parent" 2>/dev/null || true
        done

        # Refresh the hicolor cache so the desktop actually resolves Icon=$APP.
        if command -v gtk-update-icon-cache >/dev/null 2>&1; then
            gtk-update-icon-cache -f -t "$SHARE_DIR/icons/hicolor" >/dev/null 2>&1 || true
        fi
        progress 6 6 "Installing icon"
    else
        progress 6 6 "No icon configured"
    fi

    step "Installation complete"
    note "Launcher:    $WRAPPER"
    note "Executable:  $REAL_BIN"
    [ -z "$DATA_DIR" ] || note "User data:   $DATA_DIR"
    case ":$PATH:" in
        *":$BIN_DIR:"*) ;;
        *) note "$BIN_DIR is not on your PATH yet; to use '$APP', run:"
           note "export PATH=\"$BIN_DIR:\$PATH\"" ;;
    esac
}

uninstall_app() {
    [ "$PREFIX" != "/" ] || die "refusing to uninstall from '/' (pass --prefix)"

    step "Uninstalling $APP from $PREFIX"
    rm -f "$WRAPPER" "$REAL_BIN" "$DESKTOP_FILE" "$ICON_FILE"

    local rel data_note=""
    for rel in $LEGACY_ICON_RELS; do
        rm -f "$SHARE_DIR/$rel" 2>/dev/null || true
    done

    if [ "$PURGE_DATA" -eq 1 ]; then
        [ -z "$DATA_DIR" ] || { rm -rf "$DATA_DIR"; data_note="Removed user data in $DATA_DIR"; }
        [ -z "$OLD_DATA_DIR" ] || rm -rf "$OLD_DATA_DIR"
    elif [ -n "$DATA_DIR" ]; then
        data_note="Kept user data in $DATA_DIR (use --purge-data to delete it)"
    fi

    # Only remove directories that are now empty; never touch unrelated files.
    local dir
    for dir in "$ICON_DIR" "$LIB_DIR"; do
        rmdir "$dir" 2>/dev/null || true
    done
    for rel in $LEGACY_ICON_RELS; do
        rmdir "$(dirname -- "$SHARE_DIR/$rel")" 2>/dev/null || true
    done

    # Refresh the hicolor cache now that the icon is gone.
    if command -v gtk-update-icon-cache >/dev/null 2>&1; then
        gtk-update-icon-cache -f -t "$SHARE_DIR/icons/hicolor" >/dev/null 2>&1 || true
    fi

    step "Uninstall complete"
    [ -z "$data_note" ] || note "$data_note"
}

if [ "$UNINSTALL" -eq 1 ]; then
    uninstall_app
else
    if [ "$SKIP_BUILD" -eq 0 ]; then
        check_dependencies
        build
    else
        [ -x "$SRC_BIN" ] || die "no existing binary at '$SRC_BIN' to install"
    fi
    install_app
fi
