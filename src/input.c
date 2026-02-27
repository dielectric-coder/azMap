#include "input.h"
#include "projection.h"
#include <math.h>
#include <ctype.h>

static InputState *g_input = NULL;

static void scroll_callback(GLFWwindow *window, double xoffset, double yoffset)
{
    (void)window;
    (void)xoffset;
    float factor = (yoffset > 0) ? 0.9f : 1.1f;
    camera_zoom(g_input->cam, factor);
}

static void char_callback(GLFWwindow *window, unsigned int codepoint)
{
    (void)window;
    if (!g_input || !g_input->ui) return;
    UI *u = g_input->ui;
    if (!u->popup.visible || !u->popup_input_active) return;

    char ch = (char)toupper((unsigned char)codepoint);
    /* Accept A-Z, 0-9, / */
    if ((ch >= 'A' && ch <= 'Z') || (ch >= '0' && ch <= '9') || ch == '/') {
        if (u->popup_input_len < 31) {
            u->popup_input[u->popup_input_len++] = ch;
            u->popup_input[u->popup_input_len] = '\0';
        }
    }
}

static void key_callback(GLFWwindow *window, int key, int scancode, int action, int mods)
{
    (void)scancode;
    (void)mods;
    if (action != GLFW_PRESS && action != GLFW_REPEAT) return;

    /* When popup input is active, handle text editing keys and suppress others */
    if (g_input->ui && g_input->ui->popup.visible && g_input->ui->popup_input_active) {
        UI *u = g_input->ui;
        if (key == GLFW_KEY_BACKSPACE) {
            if (u->popup_input_len > 0) {
                u->popup_input[--u->popup_input_len] = '\0';
            }
        } else if (key == GLFW_KEY_ENTER || key == GLFW_KEY_KP_ENTER) {
            if (u->popup_input_len > 0) {
                u->popup_submitted = 1;
            }
        } else if (key == GLFW_KEY_ESCAPE) {
            ui_hide_popup(u);
        }
        return; /* suppress all other keys */
    }

    float step_km = g_input->cam->zoom_km * 0.05f;
    float km_per_deg = (float)(EARTH_RADIUS_KM * M_PI / 180.0);
    double dlat, dlon;

    switch (key) {
    case GLFW_KEY_LEFT:
        dlon = (double)(-step_km / (km_per_deg * cos(g_input->center_lat * M_PI / 180.0)));
        g_input->center_lon += dlon;
        if (g_input->center_lon < -180.0) g_input->center_lon += 360.0;
        g_input->center_dirty = 1;
        break;
    case GLFW_KEY_RIGHT:
        dlon = (double)(step_km / (km_per_deg * cos(g_input->center_lat * M_PI / 180.0)));
        g_input->center_lon += dlon;
        if (g_input->center_lon > 180.0) g_input->center_lon -= 360.0;
        g_input->center_dirty = 1;
        break;
    case GLFW_KEY_UP:
        dlat = (double)(step_km / km_per_deg);
        g_input->center_lat += dlat;
        if (g_input->center_lat > 90.0) g_input->center_lat = 90.0;
        g_input->center_dirty = 1;
        break;
    case GLFW_KEY_DOWN:
        dlat = (double)(-step_km / km_per_deg);
        g_input->center_lat += dlat;
        if (g_input->center_lat < -90.0) g_input->center_lat = -90.0;
        g_input->center_dirty = 1;
        break;
    case GLFW_KEY_R:
        g_input->center_lat = g_input->original_center_lat;
        g_input->center_lon = g_input->original_center_lon;
        g_input->center_dirty = 1;
        camera_reset(g_input->cam);
        break;
    case GLFW_KEY_Q:
    case GLFW_KEY_ESCAPE:
        glfwSetWindowShouldClose(window, GLFW_TRUE);
        break;
    }
}

static void mouse_button_callback(GLFWwindow *window, int button, int action, int mods)
{
    (void)mods;
    if (button == GLFW_MOUSE_BUTTON_LEFT) {
        if (action == GLFW_PRESS) {
            double mx, my;
            glfwGetCursorPos(window, &mx, &my);
            g_input->press_x = mx;
            g_input->press_y = my;
            g_input->last_mouse_x = mx;
            g_input->last_mouse_y = my;
            g_input->pressed = 1;
            g_input->dragging = 0;
            g_input->popup_dragging = 0;

            /* Check if press is on popup title bar */
            if (g_input->ui && g_input->ui->popup.visible) {
                float fb_x = (float)mx * g_input->cursor_scale_x;
                float fb_y = (float)my * g_input->cursor_scale_y;
                UIPopup *p = &g_input->ui->popup;
                float title_h = 30.0f;
                if (fb_x >= p->x && fb_x <= p->x + p->w &&
                    fb_y >= p->y && fb_y <= p->y + title_h) {
                    g_input->popup_dragging = 1;
                }
            }
        } else {
            /* Release: if no drag occurred, treat as click */
            if (g_input->pressed && !g_input->dragging && g_input->ui) {
                float fb_x = (float)g_input->press_x * g_input->cursor_scale_x;
                float fb_y = (float)g_input->press_y * g_input->cursor_scale_y;

                /* Popup intercepts clicks when visible */
                if (g_input->ui->popup.visible) {
                    UI *u = g_input->ui;
                    UIPopup *p = &u->popup;
                    /* Close button hit test */
                    if (fb_x >= u->popup_close_x &&
                        fb_x <= u->popup_close_x + u->popup_close_w &&
                        fb_y >= u->popup_close_y &&
                        fb_y <= u->popup_close_y + u->popup_close_h) {
                        ui_hide_popup(u);
                    } else if (fb_x < p->x || fb_x > p->x + p->w ||
                               fb_y < p->y || fb_y > p->y + p->h) {
                        /* Click outside popup — pass through to buttons */
                        int hit = ui_hit_test(u, fb_x, fb_y);
                        if (hit >= 0)
                            u->clicked = hit;
                    }
                    /* Click inside popup body — consume */
                } else {
                    int hit = ui_hit_test(g_input->ui, fb_x, fb_y);
                    if (hit >= 0)
                        g_input->ui->clicked = hit;
                }
            }
            g_input->pressed = 0;
            g_input->dragging = 0;
            g_input->popup_dragging = 0;
        }
    }
}

static void cursor_pos_callback(GLFWwindow *window, double xpos, double ypos)
{
    (void)window;

    /* Update hover when not pressed */
    if (!g_input->pressed && g_input->ui) {
        float fb_x = (float)xpos * g_input->cursor_scale_x;
        float fb_y = (float)ypos * g_input->cursor_scale_y;

        /* Popup close button hover */
        if (g_input->ui->popup.visible) {
            UI *u = g_input->ui;
            u->popup_close_hovered =
                (fb_x >= u->popup_close_x &&
                 fb_x <= u->popup_close_x + u->popup_close_w &&
                 fb_y >= u->popup_close_y &&
                 fb_y <= u->popup_close_y + u->popup_close_h);
            u->hovered = -1;
        } else {
            g_input->ui->hovered = ui_hit_test(g_input->ui, fb_x, fb_y);
        }
    }

    if (!g_input->pressed) return;

    /* Popup title bar drag */
    if (g_input->popup_dragging) {
        double dx = xpos - g_input->last_mouse_x;
        double dy = ypos - g_input->last_mouse_y;
        g_input->last_mouse_x = xpos;
        g_input->last_mouse_y = ypos;
        g_input->ui->popup.offset_x += (float)dx * g_input->cursor_scale_x;
        g_input->ui->popup.offset_y += (float)dy * g_input->cursor_scale_y;
        g_input->dragging = 1; /* suppress click on release */
        return;
    }

    /* Check drag threshold before starting to pan */
    if (!g_input->dragging) {
        double dx = xpos - g_input->press_x;
        double dy = ypos - g_input->press_y;
        if (dx * dx + dy * dy > 9.0) { /* 3px threshold */
            g_input->dragging = 1;
            g_input->last_mouse_x = xpos;
            g_input->last_mouse_y = ypos;
        }
        return;
    }

    double dx = xpos - g_input->last_mouse_x;
    double dy = ypos - g_input->last_mouse_y;
    g_input->last_mouse_x = xpos;
    g_input->last_mouse_y = ypos;

    /* Convert pixel delta to center lat/lon change */
    float km_per_pixel = g_input->cam->zoom_km / (float)g_input->win_height;
    float km_per_deg = (float)(EARTH_RADIUS_KM * M_PI / 180.0);
    double dlat = (double)((float)dy * km_per_pixel / km_per_deg);
    double dlon = (double)((float)(-dx) * km_per_pixel /
                  (km_per_deg * cos(g_input->center_lat * M_PI / 180.0)));
    g_input->center_lat += dlat;
    g_input->center_lon += dlon;
    if (g_input->center_lat > 90.0) g_input->center_lat = 90.0;
    if (g_input->center_lat < -90.0) g_input->center_lat = -90.0;
    if (g_input->center_lon > 180.0) g_input->center_lon -= 360.0;
    if (g_input->center_lon < -180.0) g_input->center_lon += 360.0;
    g_input->center_dirty = 1;
}

static void update_cursor_scale(GLFWwindow *window)
{
    int ww, wh;
    glfwGetWindowSize(window, &ww, &wh);
    g_input->cursor_scale_x = (ww > 0)
        ? (float)g_input->win_width / (float)ww : 1.0f;
    g_input->cursor_scale_y = (wh > 0)
        ? (float)g_input->win_height / (float)wh : 1.0f;
}

static void framebuffer_size_callback(GLFWwindow *window, int width, int height)
{
    if (height == 0) height = 1;
    g_input->win_width = width;
    g_input->win_height = height;
    g_input->cam->aspect = (float)width / (float)height;
    glViewport(0, 0, width, height);
    update_cursor_scale(window);
}

void input_init(InputState *is, GLFWwindow *window, Camera *cam, UI *ui,
                double center_lat, double center_lon)
{
    is->cam = cam;
    is->ui = ui;
    is->dragging = 0;
    is->popup_dragging = 0;
    is->pressed = 0;
    is->press_x = 0;
    is->press_y = 0;
    is->last_mouse_x = 0;
    is->last_mouse_y = 0;
    is->cursor_scale_x = 1.0f;
    is->cursor_scale_y = 1.0f;
    is->center_lat = center_lat;
    is->center_lon = center_lon;
    is->original_center_lat = center_lat;
    is->original_center_lon = center_lon;
    is->center_dirty = 0;

    int w, h;
    glfwGetFramebufferSize(window, &w, &h);
    is->win_width = w;
    is->win_height = h;

    g_input = is;
    update_cursor_scale(window);

    glfwSetScrollCallback(window, scroll_callback);
    glfwSetKeyCallback(window, key_callback);
    glfwSetCharCallback(window, char_callback);
    glfwSetMouseButtonCallback(window, mouse_button_callback);
    glfwSetCursorPosCallback(window, cursor_pos_callback);
    glfwSetFramebufferSizeCallback(window, framebuffer_size_callback);
}
