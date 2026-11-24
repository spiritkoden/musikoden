# musikoden

Minimal keyboard-driven SDL3 music player with waveform overview and live oscilloscope.
Plays MP3, FLAC, WAV, OGG/OGA and M4A/AAC.

## Install / Uninstall

```sh
./install.sh       # build + install into ~/.local
./uninstall.sh     # remove; config in ~/.config/musikoden is kept
```

Needs a C11 compiler, make, pkg-config, SDL3, SDL3_ttf, FAAD2 and fontconfig.

## Config

`~/.config/musikoden/musikoden.conf` (`bg = #101016`, `resolution = 800x480`).
