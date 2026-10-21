#include "ui.h"

#include <math.h>
#include <string.h>

#include "playlist.h"

bool ui_init(UI *ui, SDL_Renderer *r, const char *font_path, float ptsize) {
    memset(ui, 0, sizeof *ui);

    if (!TTF_Init()) {
        SDL_Log("ui: TTF_Init failed: %s", SDL_GetError());
        return false;
    }

    ui->font = TTF_OpenFont(font_path, ptsize);
    if (!ui->font) {
        SDL_Log("ui: could not open font '%s': %s", font_path, SDL_GetError());
        TTF_Quit();
        return false;
    }

    ui->engine = TTF_CreateRendererTextEngine(r);
    if (!ui->engine) {
        SDL_Log("ui: could not create text engine: %s", SDL_GetError());
        TTF_CloseFont(ui->font);
        ui->font = NULL;
        TTF_Quit();
        return false;
    }

    ui->ready = true;
    return true;
}

void ui_destroy(UI *ui) {
    if (!ui) return;
    if (ui->engine) TTF_DestroyRendererTextEngine(ui->engine);
    if (ui->font)   TTF_CloseFont(ui->font);
    if (ui->ready)  TTF_Quit();
    memset(ui, 0, sizeof *ui);
}

void ui_text(UI *ui, const char *text, float x, float y, SDL_Color color) {
    if (!ui->ready || !text || !text[0])
        return;
    TTF_Text *t = TTF_CreateText(ui->engine, ui->font, text, 0);
    if (!t)
        return;
    TTF_SetTextColor(t, color.r, color.g, color.b, color.a);
    TTF_DrawRendererText(t, x, y);
    TTF_DestroyText(t);
}

bool ui_measure(UI *ui, const char *text, int *w, int *h) {
    if (!ui->ready || !text)
        return false;
    return TTF_GetStringSize(ui->font, text, 0, w, h);
}

void ui_text_right(UI *ui, const char *text, float x_right, float y,
                   SDL_Color color) {
    int w = 0, h = 0;
    if (!ui_measure(ui, text, &w, &h))
        return;
    ui_text(ui, text, x_right - (float)w, y, color);
}

void ui_text_center(UI *ui, const char *text, float cx, float y,
                    SDL_Color color) {
    int w = 0, h = 0;
    if (!ui_measure(ui, text, &w, &h))
        return;
    ui_text(ui, text, cx - (float)w * 0.5f, y, color);
}

static const SDL_Color HELP_FG     = { 224, 232, 240, 255 };
static const SDL_Color HELP_DIM    = { 128, 142, 158, 255 };
static const SDL_Color HELP_ACCENT = {   0, 220, 180, 255 };
static const SDL_Color HELP_WARN   = { 230, 180,  60, 255 };

static bool panel_center(SDL_Renderer *r, float panel_w, float panel_h,
                         SDL_FRect *panel) {
    int rw = 0, rh = 0;
    if (!SDL_GetCurrentRenderOutputSize(r, &rw, &rh))
        return false;
    panel->x = ((float)rw - panel_w) * 0.5f;
    panel->y = ((float)rh - panel_h) * 0.5f;
    panel->w = panel_w;
    panel->h = panel_h;
    if (panel->x < 0.0f) panel->x = 0.0f;
    if (panel->y < 0.0f) panel->y = 0.0f;
    return true;
}

static void panel_paint(SDL_Renderer *r, const SDL_FRect *panel,
                        SDL_Color border) {
    SDL_SetRenderDrawColor(r, 16, 16, 22, 215);
    SDL_RenderFillRect(r, panel);
    SDL_SetRenderDrawColor(r, border.r, border.g, border.b, 255);
    SDL_RenderRect(r, panel);
}

static void overlay_panel(UI *ui, SDL_Renderer *r, SDL_Color accent,
                          const char *title, const char *message,
                          const UI_KeyHint *rows, int n,
                          const char *footnote) {
    if (!ui->ready || !r || !rows || n <= 0)
        return;

    int line_h = 0, ref_w = 0;
    if (!ui_measure(ui, "Ag", &ref_w, &line_h) || line_h <= 0)
        return;

    const float pad      = 24.0f;
    const float row_gap  = 8.0f;
    const float sect_gap = 14.0f;

    int key_col_w = 0;
    int desc_w_max = 0;
    for (int i = 0; i < n; ++i) {
        int kw = 0, kh = 0, dw = 0, dh = 0;
        ui_measure(ui, rows[i].key, &kw, &kh);
        ui_measure(ui, rows[i].desc, &dw, &dh);
        if (kw > key_col_w)  key_col_w = kw;
        if (dw > desc_w_max) desc_w_max = dw;
    }
    float body_w = (float)key_col_w + 16.0f + (float)desc_w_max;

    float panel_w = pad * 2.0f + body_w;
    float panel_h = pad * 2.0f + (float)n * (float)line_h
                  + (float)(n - 1) * row_gap;

    const char *sections[3] = { title, message, footnote };
    for (int i = 0; i < 3; ++i) {
        if (!sections[i])
            continue;
        panel_h += (float)line_h + sect_gap;
        int sw = 0, sh = 0;
        if (ui_measure(ui, sections[i], &sw, &sh) &&
            (float)sw + 2.0f * pad > panel_w)
            panel_w = (float)sw + 2.0f * pad;
    }

    SDL_FRect panel;
    if (!panel_center(r, panel_w, panel_h, &panel))
        return;
    float cx = panel.x + panel.w * 0.5f;

    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
    panel_paint(r, &panel, accent);

    float y = panel.y + pad;
    if (title) {
        ui_text_center(ui, title, cx, y, accent);
        y += (float)line_h + sect_gap;
    }
    if (message) {
        ui_text_center(ui, message, cx, y, HELP_FG);
        y += (float)line_h + sect_gap;
    }

    float key_x  = panel.x + pad;
    float desc_x = key_x + (float)key_col_w + 16.0f;
    for (int i = 0; i < n; ++i) {
        ui_text(ui, rows[i].key, key_x, y, accent);
        ui_text(ui, rows[i].desc, desc_x, y, HELP_FG);
        y += (float)line_h + row_gap;
    }

    if (footnote) {
        y += sect_gap - row_gap;
        ui_text_center(ui, footnote, cx, y, HELP_DIM);
    }

    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_NONE);
}

void ui_help_overlay(UI *ui, SDL_Renderer *r, const char *title,
                     const UI_KeyHint *hints, int n,
                     const char *footnote) {
    overlay_panel(ui, r, HELP_ACCENT, title, NULL, hints, n, footnote);
}

void ui_confirm_overlay(UI *ui, SDL_Renderer *r, const char *title,
                        const char *message, const UI_KeyHint *answers, int n,
                        const char *footnote) {
    overlay_panel(ui, r, HELP_WARN, title, message, answers, n, footnote);
}

#define LIST_ROW_GAP    2.0f
#define LIST_MIN_W    320.0f
#define LIST_MAX_H     0.70f
#define LIST_MAX_W     0.60f
#define LIST_NAME_MAX  512

void ui_playlist_overlay(UI *ui, SDL_Renderer *r,
                         char **paths, int count,
                         int current, int selected,
                         int *first_visible, UI_Scroll *marquee, float dt,
                         const char *footnote) {
    if (!ui->ready || !r || !first_visible || !marquee)
        return;
    if (count > 0 && !paths)
        return;

    int line_h = 0, ref_w = 0, gut_w = 0, gut_h = 0;
    if (!ui_measure(ui, "Ag", &ref_w, &line_h) || line_h <= 0)
        return;
    ui_measure(ui, ">", &gut_w, &gut_h);

    int rw = 0, rh = 0;
    if (!SDL_GetCurrentRenderOutputSize(r, &rw, &rh))
        return;

    const float pad      = 20.0f;
    const float sect_gap = 14.0f;
    const float gut_gap  = 10.0f;

    // widest row, so the panel hugs the list instead of the window
    float content_w = 0.0f;
    for (int i = 0; i < count; ++i) {
        char row[LIST_NAME_MAX];
        SDL_snprintf(row, sizeof row, "%d. %s", i + 1, path_basename(paths[i]));
        int tw = 0, th = 0;
        if (ui_measure(ui, row, &tw, &th) && (float)tw > content_w)
            content_w = (float)tw;
    }

    float panel_w = pad + (float)gut_w + gut_gap + content_w + pad;
    if (panel_w < LIST_MIN_W)
        panel_w = LIST_MIN_W;
    if (panel_w > (float)rw * LIST_MAX_W)
        panel_w = (float)rw * LIST_MAX_W;
    if (panel_w > (float)rw)
        panel_w = (float)rw;

    const float header_h = (float)line_h + sect_gap;
    const float footer_h = footnote ? (float)line_h + sect_gap : 0.0f;
    float avail_h  = (float)rh * LIST_MAX_H
                   - 2.0f * pad - header_h - footer_h;
    if (avail_h < (float)line_h)
        avail_h = (float)line_h;

    float body_h = (float)count * ((float)line_h + LIST_ROW_GAP) - LIST_ROW_GAP;
    if (body_h < (float)line_h)
        body_h = (float)line_h;
    if (body_h > avail_h)
        body_h = avail_h;

    int rows_visible = (int)((body_h + LIST_ROW_GAP)
                             / ((float)line_h + LIST_ROW_GAP));
    if (rows_visible < 1)
        rows_visible = 1;

    int max_first = count - rows_visible;
    if (max_first < 0)
        max_first = 0;
    if (*first_visible > max_first)
        *first_visible = max_first;
    if (selected < *first_visible)
        *first_visible = selected;
    if (selected >= *first_visible + rows_visible)
        *first_visible = selected - rows_visible + 1;
    if (*first_visible < 0)
        *first_visible = 0;

    SDL_FRect panel = {
        pad, pad,
        panel_w, 2.0f * pad + header_h + footer_h + body_h
    };
    if (!panel_center(r, panel_w, panel.h, &panel))
        return;
    float cx = panel.x + panel.w * 0.5f;

    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
    panel_paint(r, &panel, HELP_ACCENT);

    char title[64];
    SDL_snprintf(title, sizeof title, "Playlist - %d track%s",
                 count, count == 1 ? "" : "s");
    float y = panel.y + pad;
    ui_text_center(ui, title, cx, y, HELP_ACCENT);
    y += header_h;

    float rows_top = y;
    float rows_bot = rows_top + body_h;
    float text_x   = panel.x + pad + (float)gut_w + gut_gap;

    // selection bar first, full panel width, drawn before the text clip
    float bar_y = rows_top + ((float)selected - (float)*first_visible)
                            * ((float)line_h + LIST_ROW_GAP);
    if (count > 0 && selected >= *first_visible
        && bar_y + (float)line_h <= rows_bot) {
        SDL_FRect bar = { panel.x + 1.0f, bar_y,
                          panel.w - 2.0f, (float)line_h + LIST_ROW_GAP };
        SDL_SetRenderDrawColor(r, HELP_ACCENT.r, HELP_ACCENT.g,
                               HELP_ACCENT.b, 40);
        SDL_RenderFillRect(r, &bar);
    }

    SDL_Rect clip = {
        (int)SDL_roundf(text_x - gut_gap),
        (int)SDL_roundf(rows_top),
        (int)SDL_roundf(panel.w - pad - (float)gut_w - gut_gap),
        (int)SDL_roundf(body_h)
    };
    SDL_SetRenderClipRect(r, &clip);

    if (count == 0) {
        ui_text_center(ui, "no tracks - press O or drop files", cx,
                       rows_top + ((float)body_h - (float)line_h) * 0.5f,
                       HELP_DIM);
    }
    for (int i = *first_visible; i < count; ++i) {
        float row_y = rows_top + ((float)i - (float)*first_visible)
                               * ((float)line_h + LIST_ROW_GAP);
        if (row_y + (float)line_h > rows_bot)
            break;
        bool is_current = (i == current);
        if (is_current)
            ui_text(ui, ">", panel.x + pad, row_y, HELP_ACCENT);
        char row[LIST_NAME_MAX];
        SDL_snprintf(row, sizeof row, "%d. %s", i + 1, path_basename(paths[i]));
        if (i != selected) {
            ui_text(ui, row, text_x, row_y, is_current ? HELP_ACCENT : HELP_FG);
            continue;
        }
        // marquee owns the clip rect, so restore the rows clip for the rest
        SDL_FRect name_box = { text_x, row_y,
                               panel.x + panel.w - pad - text_x,
                               (float)line_h };
        ui_text_marquee(ui, r, row, &name_box, marquee, dt,
                        is_current ? HELP_ACCENT : HELP_FG);
        SDL_SetRenderClipRect(r, &clip);
    }
    SDL_SetRenderClipRect(r, NULL);

    if (footnote) {
        y = rows_bot + sect_gap;
        ui_text_center(ui, footnote, cx, y, HELP_DIM);
    }

    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_NONE);
}

#define TEXT_BOX_GAP   48.0f
#define TEXT_BOX_SPEED 60.0f

void ui_text_marquee(UI *ui, SDL_Renderer *r, const char *text,
                     const SDL_FRect *box, UI_Scroll *scroll, float dt,
                     SDL_Color fg) {
    if (!ui->ready || !r || !scroll || !box || !text || !text[0])
        return;

    int tw = 0, th = 0;
    if (!ui_measure(ui, text, &tw, &th) || tw <= 0 || th <= 0)
        return;

    float ty = box->y + (box->h - (float)th) * 0.5f;

    if ((float)tw <= box->w) {
        scroll->offset = 0.0f;
        scroll->travel = 0.0f;
        ui_text(ui, text, box->x, ty, fg);
        return;
    }

    scroll->travel = (float)tw + TEXT_BOX_GAP;
    if (scroll->offset < 0.0f || scroll->offset >= scroll->travel)
        scroll->offset = 0.0f;
    scroll->offset += TEXT_BOX_SPEED * dt;
    if (scroll->offset >= scroll->travel)
        scroll->offset = fmodf(scroll->offset, scroll->travel);

    SDL_Rect clip = {
        (int)SDL_roundf(box->x), (int)SDL_roundf(box->y),
        (int)SDL_roundf(box->w), (int)SDL_roundf(box->h)
    };
    SDL_SetRenderClipRect(r, &clip);
    ui_text(ui, text, box->x - scroll->offset, ty, fg);
    ui_text(ui, text, box->x - scroll->offset + scroll->travel, ty, fg);
    SDL_SetRenderClipRect(r, NULL);
}

void ui_text_box(UI *ui, SDL_Renderer *r, const char *text,
                 const SDL_FRect *box, UI_Scroll *scroll, float dt,
                 SDL_Color fg, SDL_Color border) {
    if (!ui->ready || !r || !scroll || !box || !text || !text[0])
        return;

    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
    panel_paint(r, box, border);

    SDL_FRect inner = { box->x + 1.0f, box->y + 1.0f,
                        box->w - 2.0f, box->h - 2.0f };
    if (inner.w < 1.0f) inner.w = 1.0f;
    if (inner.h < 1.0f) inner.h = 1.0f;

    ui_text_marquee(ui, r, text, &inner, scroll, dt, fg);

    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_NONE);
}
