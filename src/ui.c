/* ui.c — UI geometry generation: buttons, popup panel, text input.
 *
 * Builds vertex buffers each frame for the renderer.  Buttons are drawn as
 * rounded rectangles (fill via triangle fan from center, outline via GL_LINES
 * around the perimeter).  The popup is a fixed-layout dialog with title bar,
 * close button, text input field, and result lines. */

#include "ui.h"
#include "text.h"
#include <string.h>
#include <math.h>

#define CORNER_RADIUS 6.0f
#define CORNER_SEGS   6

/* Emit a rounded rectangle as triangles (triangle fan from center).
 * Returns number of vertices written. */
static int emit_rounded_rect(float *buf, float x0, float y0, float x1, float y1, float r)
{
    if (r < 1.0f) r = 1.0f;
    float cx = (x0 + x1) * 0.5f;
    float cy = (y0 + y1) * 0.5f;
    int n = 0;

    /* Build perimeter points: 4 corners with arc segments */
    float perim[4 * (CORNER_SEGS + 1) * 2]; /* x,y pairs */
    int pp = 0;
    /* Corner centers and start angles */
    float corners[4][3] = {
        { x1 - r, y0 + r, -M_PI * 0.5f }, /* top-right */
        { x1 - r, y1 - r,  0.0f },          /* bottom-right */
        { x0 + r, y1 - r,  M_PI * 0.5f },  /* bottom-left */
        { x0 + r, y0 + r,  M_PI },          /* top-left */
    };
    for (int c = 0; c < 4; c++) {
        float ccx = corners[c][0], ccy = corners[c][1], start = corners[c][2];
        for (int s = 0; s <= CORNER_SEGS; s++) {
            float a = start + (float)s * (M_PI * 0.5f) / (float)CORNER_SEGS;
            perim[pp++] = ccx + r * cosf(a);
            perim[pp++] = ccy + r * sinf(a);
        }
    }
    int npts = pp / 2;

    /* Triangle fan from center */
    for (int i = 0; i < npts; i++) {
        int j = (i + 1) % npts;
        int bi = n * 2;
        buf[bi + 0] = cx;           buf[bi + 1] = cy;
        buf[bi + 2] = perim[i*2];   buf[bi + 3] = perim[i*2+1];
        buf[bi + 4] = perim[j*2];   buf[bi + 5] = perim[j*2+1];
        n += 3;
    }
    return n;
}

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
    ui->popup.offset_x = 0;
    ui->popup.offset_y = 0;
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

/* Emit rounded rectangle outline as GL_LINES (pairs of vertices).
 * Returns number of vertices written. */
static int emit_rounded_rect_outline(float *buf, float x0, float y0, float x1, float y1, float r)
{
    if (r < 1.0f) r = 1.0f;
    /* Build perimeter points (same as fill) */
    float perim[4 * (CORNER_SEGS + 1) * 2];
    int pp = 0;
    float corners[4][3] = {
        { x1 - r, y0 + r, -M_PI * 0.5f },
        { x1 - r, y1 - r,  0.0f },
        { x0 + r, y1 - r,  M_PI * 0.5f },
        { x0 + r, y0 + r,  M_PI },
    };
    for (int c = 0; c < 4; c++) {
        float ccx = corners[c][0], ccy = corners[c][1], start = corners[c][2];
        for (int s = 0; s <= CORNER_SEGS; s++) {
            float a = start + (float)s * (M_PI * 0.5f) / (float)CORNER_SEGS;
            perim[pp++] = ccx + r * cosf(a);
            perim[pp++] = ccy + r * sinf(a);
        }
    }
    int npts = pp / 2;
    int n = 0;
    for (int i = 0; i < npts; i++) {
        int j = (i + 1) % npts;
        int bi = n * 2;
        buf[bi + 0] = perim[i*2];   buf[bi + 1] = perim[i*2+1];
        buf[bi + 2] = perim[j*2];   buf[bi + 3] = perim[j*2+1];
        n += 2;
    }
    return n;
}

void ui_build_geometry(const UI *ui,
                       float *quad_verts, int *quad_count,
                       int *btn_offsets, int *btn_counts,
                       float *outline_verts, int *outline_count,
                       int *ol_offsets, int *ol_counts,
                       float *text_verts, int *text_count,
                       int *hovered_quad)
{
    *quad_count = 0;
    *outline_count = 0;
    *text_count = 0;
    *hovered_quad = -1;

    int vis = 0; /* visible button index */
    for (int i = 0; i < ui->count; i++) {
        const UIButton *b = &ui->buttons[i];
        if (!b->visible) continue;

        /* Rounded rectangle fill */
        int offset = *quad_count;
        int nv = emit_rounded_rect(quad_verts + *quad_count * 2,
                                   b->x, b->y, b->x + b->w, b->y + b->h,
                                   CORNER_RADIUS);
        if (vis < 16) {
            btn_offsets[vis] = offset;
            btn_counts[vis] = nv;
        }
        *quad_count += nv;

        /* Rounded rectangle outline */
        int ol_offset = *outline_count;
        int ol_nv = emit_rounded_rect_outline(outline_verts + *outline_count * 2,
                                              b->x, b->y, b->x + b->w, b->y + b->h,
                                              CORNER_RADIUS);
        if (vis < 16) {
            ol_offsets[vis] = ol_offset;
            ol_counts[vis] = ol_nv;
        }
        *outline_count += ol_nv;

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

    float pw = 400.0f, base_ph = 80.0f;
    /* Expand popup height to include result lines */
    int rlines = (ui->popup_result_lines < 4) ? ui->popup_result_lines : 4;
    float ph = base_ph + (rlines > 0 ? 10.0f + rlines * 25.0f : 0.0f);
    float px = ((float)fb_w - pw) * 0.5f + ui->popup.offset_x;
    float py = ((float)fb_h - ph) * 0.5f + ui->popup.offset_y;
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

    /* Result lines (inside expanded popup area) */
    for (int i = 0; i < ui->popup_result_lines && i < 4; i++) {
        float ry = py + base_ph + 10.0f + (float)i * 25.0f;
        *text_count += text_build(ui->popup_result[i], px + 20.0f, ry, lsz,
                                  text_verts + *text_count * 2, 4096 - *text_count);
    }
}
