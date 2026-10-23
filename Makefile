# musikoden — minimal SDL3 music player with waveform visualizer

CC            ?= cc
PREFIX        ?= /usr/local
BASE_CFLAGS   ?= -std=c11 -O2 -g
DEPFLAGS      := -MMD -MP
WARN_CFLAGS   := -Wall -Wextra -Wshadow -Wconversion
PKG_CFLAGS    := $(shell pkg-config --cflags sdl3 sdl3-ttf faad2)
PKG_LIBS      := $(shell pkg-config --libs sdl3 sdl3-ttf faad2)
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
	sudo pacman -S --needed sdl3 sdl3-ttf faad2

# Terminal app: install the binary only — no desktop entry, no icon.
install: $(BIN)
	install -Dm755 $(BIN) $(DESTDIR)$(PREFIX)/bin/$(BIN)

# The two extra rm lines clean up entries left by older installs.
uninstall:
	rm -f $(DESTDIR)$(PREFIX)/bin/$(BIN)
	rm -f $(DESTDIR)$(PREFIX)/share/applications/musikoden.desktop
	rm -f $(DESTDIR)$(PREFIX)/share/icons/hicolor/300x300/apps/musikoden.png

clean:
	rm -f $(OBJ) $(DEP) $(BIN)

# Keep the dependency includes last: the first rule in an included .d file
# would otherwise become make's default goal.
-include $(DEP)
