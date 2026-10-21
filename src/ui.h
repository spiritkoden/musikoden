#ifndef UI_H
#define UI_H

#include <stdbool.h>

#include <SDL3/SDL.h>
#include <SDL3_ttf/SDL_ttf.h>

typedef struct {
    TTF_Font        *font;
    TTF_TextEngine  *engine;
    bool             ready;
} UI;

bool ui_init(UI *ui, SDL_Renderer *r, const char *font_path, float ptsize);
void ui_destroy(UI *ui);

void ui_text(UI *ui, const char *text, float x, float y,
             SDL_Color color);

bool ui_measure(UI *ui, const char *text, int *w, int *h);

void ui_text_right(UI *ui, const char *text, float x_right, float y,
                   SDL_Color color);

void ui_text_center(UI *ui, const char *text, float cx, float y,
                    SDL_Color color);

// Reset scroll when the text changes, or the new string starts mid-scroll
typedef struct {
    float offset;
    float travel;
} UI_Scroll;

void ui_text_box(UI *ui, SDL_Renderer *r, const char *text,
                 const SDL_FRect *box, UI_Scroll *scroll, float dt,
                 SDL_Color fg, SDL_Color border);

// ui_text_box without the panel: text clipped to box, marquee when it overflows
void ui_text_marquee(UI *ui, SDL_Renderer *r, const char *text,
                     const SDL_FRect *box, UI_Scroll *scroll, float dt,
                     SDL_Color fg);

typedef struct {
    const char *key;
    const char *desc;
} UI_KeyHint;

void ui_help_overlay(UI *ui, SDL_Renderer *r, const char *title,
                     const UI_KeyHint *hints, int n,
                     const char *footnote);

void ui_confirm_overlay(UI *ui, SDL_Renderer *r, const char *title,
                        const char *message, const UI_KeyHint *answers, int n,
                        const char *footnote);

// Scrollable track list. first_visible is the top row; the caller keeps it and
// this function adjusts it so `selected` stays in view. marquee animates the
// selected row's name when it is too wide for the panel.
void ui_playlist_overlay(UI *ui, SDL_Renderer *r,
                         char **paths, int count,
                         int current, int selected,
                         int *first_visible, UI_Scroll *marquee, float dt,
                         const char *footnote);

#endif // UI_H
