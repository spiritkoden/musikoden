# musikoden — minimal SDL3 music player with waveform visualizer

CC            ?= cc
PREFIX        ?= /usr/local
BASE_CFLAGS   ?= -std=c11 -O2 -g
DEPFLAGS      := -MMD -MP
WARN_CFLAGS   := -Wall -Wextra -Wshadow -Wconversion
PKG_CFLAGS    := $(shell pkg-config --cflags sdl3 sdl3-ttf faad2 fontconfig)
PKG_LIBS      := $(shell pkg-config --libs sdl3 sdl3-ttf faad2 fontconfig)
INC           := -Isrc -Ithird_party

CFLAGS        := $(BASE_CFLAGS) $(WARN_CFLAGS) $(DEPFLAGS) $(PKG_CFLAGS) $(INC)
THIRD_CFLAGS  := $(BASE_CFLAGS) -O1 -w $(DEPFLAGS) $(PKG_CFLAGS) $(INC)

LDLIBS        += $(PKG_LIBS) -lm

OBJ           := src/main.o src/decoder.o src/audio.o src/player.o \
                 src/playlist.o src/visualizer.o src/ui.o src/log.o \
                 src/config.o \
                 third_party/dr_libs_impl.o third_party/stb_vorbis.o \
                 third_party/minimp4_impl.o
DEP           := $(OBJ:.o=.d)
BIN           := musikoden

.PHONY: all clean run deps install uninstall

all: $(BIN)

$(BIN): $(OBJ)
	$(CC) $(OBJ) $(LDLIBS) -o $@

src/%.o: src/%.c
	$(CC) $(CFLAGS) -c $< -o $@

third_party/%.o: third_party/%.c
	$(CC) $(THIRD_CFLAGS) -c $< -o $@

run: $(BIN)
	./$(BIN)

deps:
	sudo pacman -S --needed sdl3 sdl3-ttf faad2 fontconfig

# Install the binary, the icon and the desktop entry. Progress lines show the
# path relative to the prefix (short and readable); the real destinations are
# still $(DESTDIR)$(PREFIX)-prefixed so packaging with DESTDIR keeps working.
BIN_REL  := bin/$(BIN)
ICON_REL := share/icons/hicolor/300x300/apps/$(BIN).png
APP_REL  := share/applications/$(BIN).desktop

install: $(BIN)
	@echo "==> Installing musikoden to $(DESTDIR)$(PREFIX)"
	@install -Dm755 $(BIN) $(DESTDIR)$(PREFIX)/$(BIN_REL)
	@echo "    ✓ $(BIN_REL)"
	@install -Dm644 img/logo.png $(DESTDIR)$(PREFIX)/$(ICON_REL)
	@echo "    ✓ $(ICON_REL)"
	@install -Dm644 musikoden.desktop $(DESTDIR)$(PREFIX)/$(APP_REL)
	@echo "    ✓ $(APP_REL)"
	@echo "==> Done"

uninstall:
	@echo "==> Uninstalling musikoden from $(DESTDIR)$(PREFIX)"
	@rm -f $(DESTDIR)$(PREFIX)/$(BIN_REL)
	@echo "    ✗ $(BIN_REL)"
	@rm -f $(DESTDIR)$(PREFIX)/$(ICON_REL)
	@echo "    ✗ $(ICON_REL)"
	@rm -f $(DESTDIR)$(PREFIX)/$(APP_REL)
	@echo "    ✗ $(APP_REL)"
	@echo "==> Done"

clean:
	rm -f $(OBJ) $(DEP) $(BIN)

# Keep the dependency includes last: the first rule in an included .d file
# would otherwise become make's default goal.
-include $(DEP)
