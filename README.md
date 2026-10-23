# musikoden

A minimal, keyboard-driven SDL3 music player with a waveform overview and live oscilloscope.
Plays MP3, FLAC, WAV, OGG/OGA and M4A/AAC.

## Build

Requires a C11 compiler, `make`, `pkg-config`, SDL3 (3.2 or newer),
SDL3_ttf and FAAD2.

```sh
make deps          # Arch Linux
make
./musikoden ~/Music
sudo make install
```

## Usage

```sh
musikoden [--font FILE] [--log FILE] <file|dir> [more...]
```

## Configuration

`~/.config/musikoden/musikoden.conf` (or `$XDG_CONFIG_HOME/musikoden/musikoden.conf`):

```ini
font =
bg = #101016
resolution = 800x480
```

`--font` and `--log` override the config.
