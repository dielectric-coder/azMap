#include "ui.h"
#include "text.h"
#include <string.h>

void ui_init(UI *ui)
{
    memset(ui, 0, sizeof(*ui));
    ui->hovered = -1;
    ui->clicked = -1;
    ui->popup_close_hovered = 0;
}

void ui_popup_clear_input(UI *ui)
{
    memset(ui->popup_input, 0, sizeof(ui->popup_input));
    ui->popup_input_len = 0;
    memset(ui->popup_result, 0, sizeof(ui->popup_result));
    ui->popup_result_lines = 0;
    ui->popup_submitted = 0;
}

void ui_show_popup(UI *ui, const char *title)
{
    strncpy(ui->popup.title, title, sizeof(ui->popup.title) - 1);
    ui->popup.title[sizeof(ui->popup.title) - 1] = '\0';
    ui->popup.visible = 1;
    ui_popup_clear_input(ui);
    ui->popup_input_active = 1;
}

void ui_hide_popup(UI *ui)
{
    ui->popup.visible = 0;
    ui->popup_close_hovered = 0;
}

int ui_add_button(UI *ui, const char *label, float x, float y, float w, float h)
{
    if (ui->count >= UI_MAX_BUTTONS) return -1;
    int idx = ui->count++;
    UIButton *b = &ui->buttons[idx];
    strncpy(b->label, label, sizeof(b->label) - 1);
    b->label[sizeof(b->label) - 1] = '\0';
    b->x = x;
    b->y = y;
    b->w = w;
    b->h = h;
    b->visible = 1;
    return idx;
}

int ui_hit_test(const UI *ui, float mx, float my)
{
    for (int i = 0; i < ui->count; i++) {
        const UIButton *b = &ui->buttons[i];
        if (!b->visible) continue;
        if (mx >= b->x && mx <= b->x + b->w &&
            my >= b->y && my <= b->y + b->h)
            return i;
    }
    return -1;
}

void ui_build_geometry(const UI *ui,
                       float *quad_verts, int *quad_count,
                       float *text_verts, int *text_count,
                       int *hovered_quad)
{
    *quad_count = 0;
    *text_count = 0;
    *hovered_quad = -1;

    int vis = 0; /* visible button index */
    for (int i = 0; i < ui->count; i++) {
        const UIButton *b = &ui->buttons[i];
        if (!b->visible) continue;

        /* Background quad (2 triangles, 6 vertices) */
        float x0 = b->x, y0 = b->y;
        float x1 = b->x + b->w, y1 = b->y + b->h;
        int qi = *quad_count * 2;
        quad_verts[qi+ 0] = x0; quad_verts[qi+ 1] = y0;
        quad_verts[qi+ 2] = x1; quad_verts[qi+ 3] = y0;
        quad_verts[qi+ 4] = x1; quad_verts[qi+ 5] = y1;
        quad_verts[qi+ 6] = x0; quad_verts[qi+ 7] = y0;
        quad_verts[qi+ 8] = x1; quad_verts[qi+ 9] = y1;
        quad_verts[qi+10] = x0; quad_verts[qi+11] = y1;
        *quad_count += 6;

        if (i == ui->hovered)
            *hovered_quad = vis;

        /* Text centered in button */
        float text_size = b->h * 0.55f;
        float tw = text_width(b->label, text_size);
        float txt_x = b->x + (b->w - tw) * 0.5f;
        float txt_y = b->y + (b->h - text_size) * 0.5f;
        int tv = text_build(b->label, txt_x, txt_y, text_size,
                            text_verts + *text_count * 2,
                            4096 - *text_count);
        *text_count += tv;

        vis++;
    }
}

/* Helper: emit a quad (2 triangles, 6 vertices) into buf at offset *n (in vertices). */
static void emit_quad(float *buf, int *n, float x0, float y0, float x1, float y1)
{
    int i = *n * 2;
    buf[i+ 0] = x0; buf[i+ 1] = y0;
    buf[i+ 2] = x1; buf[i+ 3] = y0;
    buf[i+ 4] = x1; buf[i+ 5] = y1;
    buf[i+ 6] = x0; buf[i+ 7] = y0;
    buf[i+ 8] = x1; buf[i+ 9] = y1;
    buf[i+10] = x0; buf[i+11] = y1;
    *n += 6;
}

void ui_build_popup_geometry(UI *ui, int fb_w, int fb_h,
                             float *quad_verts, int *quad_count,
                             float *text_verts, int *text_count)
{
    *quad_count = 0;
    *text_count = 0;

    float pw = 400.0f, ph = 300.0f;
    float px = ((float)fb_w - pw) * 0.5f;
    float py = ((float)fb_h - ph) * 0.5f;
    float title_h = 30.0f;
    float close_sz = 24.0f;

    /* Store bounds for hit-testing */
    ui->popup.x = px;
    ui->popup.y = py;
    ui->popup.w = pw;
    ui->popup.h = ph;

    /* Quad 0: body */
    emit_quad(quad_verts, quad_count, px, py + title_h, px + pw, py + ph);

    /* Quad 1: title bar */
    emit_quad(quad_verts, quad_count, px, py, px + pw, py + title_h);

    /* Quad 2: close button (top-right of title bar) */
    float cx = px + pw - close_sz - 3.0f;
    float cy = py + (title_h - close_sz) * 0.5f;
    ui->popup_close_x = cx;
    ui->popup_close_y = cy;
    ui->popup_close_w = close_sz;
    ui->popup_close_h = close_sz;
    emit_quad(quad_verts, quad_count, cx, cy, cx + close_sz, cy + close_sz);

    /* Quad 3: input box background */
    float input_x = px + 90.0f;
    float input_y = py + 45.0f;
    float input_w = 280.0f;
    float input_h = 25.0f;
    emit_quad(quad_verts, quad_count, input_x, input_y,
              input_x + input_w, input_y + input_h);

    /* Title text (centered in title bar) */
    float tsz = title_h * 0.55f;
    float ttw = text_width(ui->popup.title, tsz);
    float ttx = px + (pw - ttw) * 0.5f;
    float tty = py + (title_h - tsz) * 0.5f;
    *text_count += text_build(ui->popup.title, ttx, tty, tsz,
                              text_verts + *text_count * 2, 4096 - *text_count);

    /* "X" text (centered in close button) */
    float xsz = close_sz * 0.55f;
    float xw = text_width("X", xsz);
    float xx = cx + (close_sz - xw) * 0.5f;
    float xy = cy + (close_sz - xsz) * 0.5f;
    *text_count += text_build("X", xx, xy, xsz,
                              text_verts + *text_count * 2, 4096 - *text_count);

    /* "CALL:" label */
    float lsz = 16.0f;
    *text_count += text_build("CALL:", px + 20.0f, py + 50.0f, lsz,
                              text_verts + *text_count * 2, 4096 - *text_count);

    /* Typed text inside input box */
    if (ui->popup_input_len > 0) {
        *text_count += text_build(ui->popup_input, input_x + 5.0f, input_y + 4.0f, lsz,
                                  text_verts + *text_count * 2, 4096 - *text_count);
    }

    /* Cursor after last character */
    if (ui->popup_input_active) {
        float cur_x = input_x + 5.0f + text_width(ui->popup_input, lsz);
        *text_count += text_build("_", cur_x, input_y + 4.0f, lsz,
                                  text_verts + *text_count * 2, 4096 - *text_count);
    }

    /* Result lines */
    for (int i = 0; i < ui->popup_result_lines && i < 4; i++) {
        float ry = py + 90.0f + (float)i * 25.0f;
        *text_count += text_build(ui->popup_result[i], px + 20.0f, ry, lsz,
                                  text_verts + *text_count * 2, 4096 - *text_count);
    }
}
