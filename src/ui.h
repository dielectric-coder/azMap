#ifndef UI_H
#define UI_H

#define UI_MAX_BUTTONS 16

typedef struct {
    float x, y, w, h;       /* pixel-space bounds (framebuffer coords) */
    char  label[32];         /* button text */
    int   visible;           /* whether to draw/test */
} UIButton;

typedef struct {
    float x, y, w, h;       /* pixel-space bounds (computed each frame) */
    char  title[32];
    int   visible;
} UIPopup;

typedef struct {
    UIButton buttons[UI_MAX_BUTTONS];
    int      count;
    int      hovered;        /* index of hovered button (-1 = none) */
    int      clicked;        /* index of button clicked this frame (-1 = none) */

    UIPopup  popup;
    float    popup_close_x, popup_close_y, popup_close_w, popup_close_h;
    int      popup_close_hovered;
} UI;

/* Zero out state. */
void ui_init(UI *ui);

/* Register a button. Returns its index, or -1 if full. */
int ui_add_button(UI *ui, const char *label, float x, float y, float w, float h);

/* Hit-test a framebuffer-space point against visible buttons.
 * Returns button index or -1. */
int ui_hit_test(const UI *ui, float mx, float my);

/* Build renderable geometry for all visible buttons.
 * quad_verts:  output buffer for background quads (GL_TRIANGLES, 2 floats/vert).
 * quad_count:  receives total quad vertices written.
 * text_verts:  output buffer for label text (GL_LINES, 2 floats/vert).
 * text_count:  receives total text vertices written.
 * hovered_quad: receives visible-button index of hovered button (-1 if none). */
void ui_build_geometry(const UI *ui,
                       float *quad_verts, int *quad_count,
                       float *text_verts, int *text_count,
                       int *hovered_quad);

/* Show a centered popup panel with the given title. */
void ui_show_popup(UI *ui, const char *title);

/* Hide the popup panel. */
void ui_hide_popup(UI *ui);

/* Build popup geometry: body + title bar + close button quads, title + "X" text.
 * Stores close-button bounds in ui for hit-testing (casts away const internally). */
void ui_build_popup_geometry(UI *ui, int fb_w, int fb_h,
                             float *quad_verts, int *quad_count,
                             float *text_verts, int *text_count);

#endif
