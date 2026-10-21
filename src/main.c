// geteuid() is POSIX; expose it under -std=c11.
#define _POSIX_C_SOURCE 200809L

#include <SDL3/SDL.h>
#include <SDL3/SDL_dialog.h>
#include <SDL3_ttf/SDL_ttf.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "config.h"
#include "log.h"
#include "player.h"
#include "playlist.h"
#include "ui.h"
#include "visualizer.h"

#define WINDOW_TITLE "musikoden"

#define SEEK_STEP_SECONDS 5.0
#define VOLUME_STEP       0.05f
#define STATUS_MS         2500
#define LIST_PAGE         10

#define MARGIN     24.0f
#define OVERVIEW_H 64.0f
// full scope tap, so the trace fills the box at any window width
#define SCOPE_SAMPLES AUDIO_SCOPE_CAP

#define ELEMENT_GAP 3.0f
#define BOX_TOP_GAP 6.0f

#define TEXT_ROW_H    22.0f
#define TEXT_ROW2_Y   (MARGIN + TEXT_ROW_H)
#define TEXT_ROW3_Y   (TEXT_ROW2_Y + TEXT_ROW_H)
#define TEXT_BLOCK_H  (TEXT_ROW_H + 24.0f)

static const char *DEFAULT_FONTS[] = {
    "/usr/share/fonts/TTF/DejaVuSansMono.ttf",
    "/usr/share/fonts/TTF/DejaVuSans.ttf",
    "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
    "/usr/share/fonts/TTF/LiberationSans-Regular.ttf",
    NULL,
};

static const SDL_Color COL_FG    = { 224, 232, 240, 255 };
static const SDL_Color COL_DIM   = { 128, 142, 158, 255 };
static const SDL_Color COL_ACCENT= {   0, 220, 180, 255 };

// must outlive the async dialog callback
static const SDL_DialogFileFilter DIALOG_FILTERS[] = {
    { "Audio Files", "mp3;flac;wav;ogg;oga;m4a" },
    { "All Files", "*" },
};

static const UI_KeyHint HELP_HINTS[] = {
    { "Space",      "play / pause" },
    { "Left / Right", "seek -5s / +5s" },
    { "Home / End", "seek to start / end" },
    { "Up / Down",  "volume up / down" },
    { "N / P",      "next / previous track" },
    { "S",          "show playlist, pick a track" },
    { "L",          "toggle loop" },
    { "O",          "open files / directories" },
    { "W",          "save window size as default" },
    { "M",          "minimize window" },
    { "H",          "show / hide this help" },
    { "Q / Esc",    "ask to quit" },
};

static const UI_KeyHint QUIT_ANSWERS[] = {
    { "Y", "quit musikoden" },
    { "N", "keep listening" },
    { "M", "minimize instead" },
};
static const UI_KeyHint SAVE_ANSWERS[] = {
    { "Y", "save" },
    { "N", "cancel" },
};

typedef enum {
    OVL_NONE,
    OVL_HELP,
    OVL_LIST,
    OVL_QUIT,
    OVL_SAVE,
} Overlay;

// dialog results marshaled from the (possibly foreign) dialog thread;
// user.data1 = malloc'd paths, or NULL on error
static Uint32 dialog_event_type = 0;

static void file_dialog_callback(void *userdata, const char * const *filelist, int filter) {
    (void)userdata;
    (void)filter;

    char **copy = NULL;
    if (filelist && filelist[0]) {
        int n = 0;
        while (filelist[n])
            n++;
        copy = (char **)malloc((size_t)(n + 1) * sizeof(char *));
        if (copy) {
            int j = 0;
            for (int i = 0; i < n; ++i) {
                char *s = strdup(filelist[i]);
                if (s)
                    copy[j++] = s;
            }
            copy[j] = NULL;
        }
    }

    SDL_Event ev;
    SDL_zero(ev);
    ev.type = dialog_event_type;
    ev.user.data1 = copy;
    SDL_PushEvent(&ev);
}

static void seek_to_x(Player *p, const SDL_FRect *rect, float x) {
    double frac = (double)(x - rect->x) / (double)rect->w;
    if (frac < 0.0) frac = 0.0;
    if (frac > 1.0) frac = 1.0;
    player_seek_seconds(p, frac * player_duration(p));
}

int main(int argc, char *argv[]) {
    const char *font_arg = NULL;
    const char *log_arg  = NULL;
    int first_arg = 1;
    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--font") == 0 && i + 1 < argc) {
            font_arg = argv[++i];
            first_arg = i + 1;
        } else if (strcmp(argv[i], "--log") == 0 && i + 1 < argc) {
            log_arg = argv[++i];
            first_arg = i + 1;
        } else {
            break;
        }
    }

    Config cfg;
    if (!config_load(&cfg))
        SDL_Log("config: persistence disabled (no usable config path)");

    const char *font_path = font_arg ? font_arg
                                     : (cfg.font[0] ? cfg.font : NULL);
    if (!font_path) {
        for (int i = 0; DEFAULT_FONTS[i]; ++i) {
            FILE *f = fopen(DEFAULT_FONTS[i], "r");
            if (f) { fclose(f); font_path = DEFAULT_FONTS[i]; break; }
        }
    }
    const char *log_path = log_arg ? log_arg
                                   : (cfg.log[0] ? cfg.log : "musikoden.log");

    if (!log_open(log_path))
        SDL_Log("warning: could not open log file '%s'; logging to stderr only",
                log_path);
    SDL_Log("musikoden starting: log='%s' font='%s'", log_path,
            font_path ? font_path : "(none)");
    if (cfg.path[0])
        SDL_Log("config: '%s' (font='%s' log='%s' bg=#%06x resolution=%dx%d)",
                cfg.path, cfg.font[0] ? cfg.font : "(auto)",
                cfg.log[0] ? cfg.log : "musikoden.log",
                (unsigned)cfg.bg, cfg.width, cfg.height);

    Playlist playlist;
    playlist_build(&playlist, argv, argc, first_arg);
    if (playlist.count == 0 && first_arg < argc) {
        fprintf(stderr, "usage: %s [--font FILE] [--log FILE] <file|dir> [more...]\n",
                argv[0]);
        SDL_Log("no playable tracks given; exiting");
        log_close();
        playlist_free(&playlist);
        return 2;
    }
    SDL_Log("playlist: %d track(s) from arguments", playlist.count);

    // root would make SDL grab raw ALSA and hold it, so restrict the probe
    if (geteuid() == 0) {
        SDL_Log("warning: running as root — audio may bypass the desktop "
                "sound server and break speaker output for other apps. "
                "Run musikoden as your desktop user instead.");
        SDL_SetHint(SDL_HINT_AUDIO_DRIVER, "pipewire,pulseaudio");
    }

    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO)) {
        SDL_Log("SDL_Init failed: %s", SDL_GetError());
        playlist_free(&playlist);
        return 1;
    }

    SDL_Window *window = SDL_CreateWindow(
        WINDOW_TITLE, cfg.width, cfg.height,
        SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY);
    if (!window) {
        SDL_Log("SDL_CreateWindow failed: %s", SDL_GetError());
        SDL_Quit();
        playlist_free(&playlist);
        return 1;
    }
    SDL_Renderer *renderer = SDL_CreateRenderer(window, NULL);
    if (!renderer) {
        SDL_Log("SDL_CreateRenderer failed: %s", SDL_GetError());
        SDL_DestroyWindow(window);
        SDL_Quit();
        playlist_free(&playlist);
        return 1;
    }
    SDL_SetRenderVSync(renderer, 1);

    UI ui;
    if (!ui_init(&ui, renderer, font_path, 18.0f))
        SDL_Log("running without text overlay");

    Player player;
    if (!player_init(&player, playlist.items, playlist.count,
                     playlist.count > 0 ? 0 : -1))
        SDL_Log("failed to open first track");

    dialog_event_type = SDL_RegisterEvents(1);
    if (dialog_event_type == (Uint32)-1) {
        dialog_event_type = 0;
        SDL_Log("failed to register dialog event; 'o' open dialog disabled");
    }

    if (playlist.count == 0 && dialog_event_type != 0) {
        SDL_Log("no tracks given; opening file dialog");
        SDL_ShowOpenFileDialog(file_dialog_callback, NULL, window,
                               DIALOG_FILTERS,
                               (int)(sizeof DIALOG_FILTERS / sizeof DIALOG_FILTERS[0]),
                               NULL, true);
    }

    float *scope_buf = (float *)malloc((size_t)SCOPE_SAMPLES * sizeof(float));
    WaveOverview overview = { 0 };
    const float *ov_built_for = NULL;
    int ov_built_columns = -1;

    bool running = true;
    bool scrubbing = false;
    Overlay overlay = OVL_NONE;
    int save_w = 0, save_h = 0;
    char status_msg[64] = { 0 };
    Uint64 status_until = 0;
    UI_Scroll name_scroll = { 0 };
    int name_for = -2;
    int list_sel = 0;
    int list_first = 0;
    UI_Scroll list_scroll = { 0 };
    int list_sel_for = -2;
    const char *list_name_for = NULL;
    Uint64 last_ticks = SDL_GetTicks();

    while (running) {
        Uint64 now = SDL_GetTicks();
        float dt = (float)(now - last_ticks) / 1000.0f;
        if (dt > 0.1f) dt = 0.1f;
        last_ticks = now;

        int w = 0, h = 0;
        SDL_GetCurrentRenderOutputSize(renderer, &w, &h);
        SDL_FRect overview_rect = {
            MARGIN, (float)h - MARGIN - OVERVIEW_H,
            (float)w - 2.0f * MARGIN, OVERVIEW_H
        };
        if (overview_rect.w < 1.0f) overview_rect.w = 1.0f;
        if (overview_rect.y < 0.0f) overview_rect.y = 0.0f;

        SDL_Event ev;
        while (SDL_PollEvent(&ev)) {
            if (dialog_event_type != 0 && ev.type == dialog_event_type) {
                char **files = (char **)ev.user.data1;
                if (!files) {
                    SDL_Log("file dialog: error or dismissed");
                }
                if (files) {
                    int n = 0;
                    while (files[n])
                        n++;
                    if (n == 0)
                        SDL_Log("file dialog: cancelled (no selection)");
                    for (int i = 0; i < n; ++i)
                        SDL_Log("file dialog: selected '%s'", files[i]);
                    Playlist picked = { 0 };
                    playlist_build(&picked, files, n, 0);
                    if (picked.count > 0) {
                        SDL_Log("file dialog: loading %d track(s)", picked.count);
                        Playlist old = playlist;
                        playlist = picked;
                        // paths is borrowed; only the playlist frees it
                        player.paths  = playlist.items;
                        player.count  = playlist.count;
                        player.current = -1;
                        (void)player_open(&player, 0);
                        playlist_free(&old);
                    } else {
                        playlist_free(&picked);
                    }
                    for (int i = 0; files[i]; ++i)
                        free(files[i]);
                    free(files);
                }
                continue;
            }

            switch (ev.type) {
            case SDL_EVENT_QUIT:
            case SDL_EVENT_WINDOW_CLOSE_REQUESTED:
                running = false;
                break;

            case SDL_EVENT_KEY_DOWN:
                // the playlist list is the one place repeats are wanted
                if (ev.key.repeat && overlay != OVL_LIST)
                    break;

                if (overlay == OVL_QUIT || overlay == OVL_SAVE) {
                    Overlay box = overlay;
                    if (ev.key.key == SDLK_Y) {
                        overlay = OVL_NONE;
                        if (box == OVL_QUIT) {
                            running = false;
                        } else if (config_save_resolution(&cfg, save_w, save_h)) {
                            SDL_snprintf(status_msg, sizeof status_msg,
                                         "saved resolution %dx%d", save_w, save_h);
                            status_until = SDL_GetTicks() + STATUS_MS;
                        } else {
                            SDL_snprintf(status_msg, sizeof status_msg,
                                         "could not save resolution");
                            status_until = SDL_GetTicks() + STATUS_MS;
                        }
                    } else if (ev.key.key == SDLK_N) {
                        overlay = OVL_NONE;
                    } else if (ev.key.key == SDLK_M && box == OVL_QUIT) {
                        overlay = OVL_NONE;
                        if (!SDL_MinimizeWindow(window))
                            SDL_Log("SDL_MinimizeWindow failed: %s", SDL_GetError());
                    }
                    break;
                }

                if (overlay == OVL_HELP) {
                    if (ev.key.key == SDLK_H || ev.key.key == SDLK_ESCAPE)
                        overlay = OVL_NONE;
                    break;
                }

                if (overlay == OVL_LIST) {
                    bool move = true;
                    switch (ev.key.key) {
                    case SDLK_UP:       list_sel--; break;
                    case SDLK_DOWN:     list_sel++; break;
                    case SDLK_PAGEUP:   list_sel -= LIST_PAGE; break;
                    case SDLK_PAGEDOWN: list_sel += LIST_PAGE; break;
                    case SDLK_HOME:     list_sel = 0; break;
                    case SDLK_END:      list_sel = player.count - 1; break;
                    default: move = false; break;
                    }
                    if (move || ev.key.repeat)
                        break;
                    switch (ev.key.key) {
                    case SDLK_S:
                    case SDLK_ESCAPE:
                        overlay = OVL_NONE;
                        break;
                    case SDLK_SPACE:
                    case SDLK_RETURN:
                    case SDLK_KP_ENTER:
                        if (player_open(&player, list_sel))
                            overlay = OVL_NONE;
                        break;
                    default: break;
                    }
                    break;
                }

                switch (ev.key.key) {
                case SDLK_ESCAPE:
                case SDLK_Q:     overlay = OVL_QUIT; break;
                case SDLK_SPACE: player_toggle(&player); break;
                case SDLK_LEFT:  player_seek_relative(&player, -SEEK_STEP_SECONDS); break;
                case SDLK_RIGHT: player_seek_relative(&player, +SEEK_STEP_SECONDS); break;
                case SDLK_HOME:  player_seek_seconds(&player, 0.0); break;
                case SDLK_END:   player_seek_seconds(&player, player_duration(&player)); break;
                case SDLK_UP:    player_adjust_volume(&player, +VOLUME_STEP); break;
                case SDLK_DOWN:  player_adjust_volume(&player, -VOLUME_STEP); break;
                case SDLK_N:     player_next(&player); break;
                case SDLK_P:     player_prev(&player); break;
                case SDLK_L:     player.loop_track = !player.loop_track; break;
                case SDLK_W:
                    SDL_GetWindowSize(window, &save_w, &save_h);
                    overlay = OVL_SAVE;
                    break;
                case SDLK_M:
                    if (!SDL_MinimizeWindow(window))
                        SDL_Log("SDL_MinimizeWindow failed: %s", SDL_GetError());
                    break;
                case SDLK_H:     overlay = OVL_HELP; break;
                case SDLK_S:
                    list_sel   = player.current >= 0 ? player.current : 0;
                    list_first = 0;
                    overlay    = OVL_LIST;
                    break;
                case SDLK_O:
                    if (dialog_event_type) {
                        SDL_Log("file dialog: opening");
                        SDL_ShowOpenFileDialog(file_dialog_callback, NULL, window,
                                               DIALOG_FILTERS,
                                               (int)(sizeof DIALOG_FILTERS / sizeof DIALOG_FILTERS[0]),
                                               NULL, true);
                    }
                    break;
                default: break;
                }
                break;

            case SDL_EVENT_MOUSE_BUTTON_DOWN:
                if (ev.button.button == SDL_BUTTON_LEFT) {
                    float mx = ev.button.x, my = ev.button.y;
                    if (mx >= overview_rect.x && mx <= overview_rect.x + overview_rect.w &&
                        my >= overview_rect.y && my <= overview_rect.y + overview_rect.h) {
                        scrubbing = true;
                        seek_to_x(&player, &overview_rect, mx);
                    }
                }
                break;
            case SDL_EVENT_MOUSE_BUTTON_UP:
                if (ev.button.button == SDL_BUTTON_LEFT)
                    scrubbing = false;
                break;
            case SDL_EVENT_MOUSE_MOTION:
                if (scrubbing)
                    seek_to_x(&player, &overview_rect, ev.motion.x);
                break;

            case SDL_EVENT_DROP_FILE:
                if (ev.drop.data) {
                    Playlist dropped = { 0 };
                    char *one[1] = { (char *)ev.drop.data };
                    playlist_build(&dropped, one, 1, 0);
                    if (dropped.count > 0) {
                        Playlist old = playlist;
                        playlist = dropped;
                        // paths is borrowed; only the playlist frees it
                        player.paths = playlist.items;
                        player.count = playlist.count;
                        (void)player_open(&player, 0);
                        playlist_free(&old);
                    } else {
                        playlist_free(&dropped);
                    }
                }
                break;

            default:
                break;
            }
        }

        player_tick(&player);

        int columns = (int)overview_rect.w;
        if (columns != ov_built_columns || player.track.samples != ov_built_for) {
            overview_build(&overview, &player.track, columns);
            ov_built_columns = columns;
            ov_built_for = player.track.samples;
        }

        SDL_SetRenderDrawColor(renderer,
                               (Uint8)((cfg.bg >> 16) & 0xFF),
                               (Uint8)((cfg.bg >> 8) & 0xFF),
                               (Uint8)(cfg.bg & 0xFF), 255);
        SDL_RenderClear(renderer);

        double dur = player_duration(&player);
        double pos = player_position(&player);
        double progress = dur > 0.0 ? pos / dur : 0.0;
        overview_draw(renderer, &overview, &overview_rect, progress);

        int line_h = (int)TEXT_ROW_H;
        int ref_w = 0, ref_h = 0;
        if (ui_measure(&ui, "Ag", &ref_w, &ref_h) && ref_h > 0)
            line_h = ref_h;

        float time_y = overview_rect.y - (float)line_h - ELEMENT_GAP;
        if (time_y < TEXT_ROW3_Y)
            time_y = TEXT_ROW3_Y;

        if (player.current != name_for) {
            name_for = player.current;
            name_scroll = (UI_Scroll){ 0 };
        }
        SDL_FRect name_box = {
            MARGIN, MARGIN + (float)line_h + BOX_TOP_GAP,
            (float)w - 2.0f * MARGIN, (float)line_h + 8.0f
        };
        if (name_box.w < 1.0f) name_box.w = 1.0f;

        float scope_top = MARGIN + TEXT_BLOCK_H + MARGIN;
        if (name_box.y + name_box.h + ELEMENT_GAP > scope_top)
            scope_top = name_box.y + name_box.h + ELEMENT_GAP;
        SDL_FRect scope_rect = {
            MARGIN, scope_top, overview_rect.w,
            (time_y - ELEMENT_GAP) - scope_top
        };
        if (scope_rect.h > 1.0f) {
            audio_scope_read(&player.audio, scope_buf, SCOPE_SAMPLES);
            scope_draw(renderer, scope_buf, SCOPE_SAMPLES, &scope_rect);
        }

        char pos_s[32], dur_s[32];
        format_time(pos, pos_s, sizeof pos_s);
        format_time(dur, dur_s, sizeof dur_s);

        char timebuf[80];
        snprintf(timebuf, sizeof timebuf, "%s / %s", pos_s, dur_s);

        char statbuf[96];
        snprintf(statbuf, sizeof statbuf, "vol %3.0f%%%s%s",
                 (double)(player.volume * 100.0f),
                 player.playing ? "  play" : "  pause",
                 player.loop_track ? "  loop" : "");
        char idxbuf[32];
        snprintf(idxbuf, sizeof idxbuf, "%d/%d", player.current + 1, player.count);

        ui_text(&ui, idxbuf, MARGIN, MARGIN, COL_ACCENT);
        ui_text_right(&ui, statbuf, (float)w - MARGIN, MARGIN, COL_DIM);
        ui_text_box(&ui, renderer, player_current_name(&player), &name_box,
                    &name_scroll, dt, COL_FG, COL_DIM);

        ui_text_right(&ui, timebuf, overview_rect.x + overview_rect.w,
                      time_y, COL_DIM);

        if (status_msg[0] && now < status_until)
            ui_text(&ui, status_msg, MARGIN, time_y, COL_DIM);

        switch (overlay) {
        case OVL_HELP:
            ui_help_overlay(&ui, renderer, "Keyboard shortcuts",
                            HELP_HINTS,
                            (int)(sizeof HELP_HINTS / sizeof HELP_HINTS[0]),
                            "H to close");
            break;
        case OVL_LIST:
            if (list_sel > playlist.count - 1)
                list_sel = playlist.count - 1;
            if (list_sel < 0)
                list_sel = 0;
            // restart the marquee when the highlighted row changes, or when
            // the same index now holds a different file
            if (list_sel != list_sel_for ||
                (list_sel < playlist.count &&
                 playlist.items[list_sel] != list_name_for)) {
                list_sel_for  = list_sel;
                list_name_for = list_sel < playlist.count
                                    ? playlist.items[list_sel] : NULL;
                list_scroll = (UI_Scroll){ 0 };
            }
            ui_playlist_overlay(&ui, renderer, playlist.items, playlist.count,
                                player.current, list_sel, &list_first,
                                &list_scroll, dt, NULL);
            break;
        case OVL_QUIT:
            ui_confirm_overlay(&ui, renderer, "Quit?",
                               "Playback will stop.", QUIT_ANSWERS,
                               (int)(sizeof QUIT_ANSWERS / sizeof QUIT_ANSWERS[0]),
                               NULL);
            break;
        case OVL_SAVE: {
            char msgbuf[64];
            SDL_snprintf(msgbuf, sizeof msgbuf,
                         "%dx%d becomes the default on the next start.",
                         save_w, save_h);
            ui_confirm_overlay(&ui, renderer, "Save window size?", msgbuf,
                               SAVE_ANSWERS,
                               (int)(sizeof SAVE_ANSWERS / sizeof SAVE_ANSWERS[0]),
                               NULL);
            break;
        }
        default:
            break;
        }

        SDL_RenderPresent(renderer);
    }

    SDL_Log("musikoden shutting down");
    free(scope_buf);
    overview_free(&overview);
    ui_destroy(&ui);
    player_destroy(&player);
    playlist_free(&playlist);
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
    log_close();
    return 0;
}
