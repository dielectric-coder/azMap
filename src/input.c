#include "input.h"
#include <math.h>

static InputState *g_input = NULL;

static void scroll_callback(GLFWwindow *window, double xoffset, double yoffset)
{
    (void)window;
    (void)xoffset;
    float factor = (yoffset > 0) ? 0.9f : 1.1f;
    camera_zoom(g_input->cam, factor);
}

static void key_callback(GLFWwindow *window, int key, int scancode, int action, int mods)
{
    (void)scancode;
    (void)mods;
    if (action != GLFW_PRESS && action != GLFW_REPEAT) return;

    float pan_step = g_input->cam->zoom_km * 0.05f;

    switch (key) {
    case GLFW_KEY_LEFT:  camera_pan(g_input->cam, -pan_step, 0); break;
    case GLFW_KEY_RIGHT: camera_pan(g_input->cam, pan_step, 0); break;
    case GLFW_KEY_UP:    camera_pan(g_input->cam, 0, pan_step); break;
    case GLFW_KEY_DOWN:  camera_pan(g_input->cam, 0, -pan_step); break;
    case GLFW_KEY_R:     camera_reset(g_input->cam); break;
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
            g_input->dragging = 1;
            glfwGetCursorPos(window, &g_input->last_mouse_x, &g_input->last_mouse_y);
        } else {
            g_input->dragging = 0;
        }
    }
}

static void cursor_pos_callback(GLFWwindow *window, double xpos, double ypos)
{
    (void)window;
    if (!g_input->dragging) return;

    double dx = xpos - g_input->last_mouse_x;
    double dy = ypos - g_input->last_mouse_y;
    g_input->last_mouse_x = xpos;
    g_input->last_mouse_y = ypos;

    /* Convert pixel delta to km: pixels / window_size * zoom_km */
    float km_per_pixel = g_input->cam->zoom_km / (float)g_input->win_height;
    camera_pan(g_input->cam, (float)(-dx) * km_per_pixel, (float)(dy) * km_per_pixel);
}

static void framebuffer_size_callback(GLFWwindow *window, int width, int height)
{
    (void)window;
    if (height == 0) height = 1;
    g_input->win_width = width;
    g_input->win_height = height;
    g_input->cam->aspect = (float)width / (float)height;
    glViewport(0, 0, width, height);
}

void input_init(InputState *is, GLFWwindow *window, Camera *cam)
{
    is->cam = cam;
    is->dragging = 0;
    is->last_mouse_x = 0;
    is->last_mouse_y = 0;

    int w, h;
    glfwGetFramebufferSize(window, &w, &h);
    is->win_width = w;
    is->win_height = h;

    g_input = is;

    glfwSetScrollCallback(window, scroll_callback);
    glfwSetKeyCallback(window, key_callback);
    glfwSetMouseButtonCallback(window, mouse_button_callback);
    glfwSetCursorPosCallback(window, cursor_pos_callback);
    glfwSetFramebufferSizeCallback(window, framebuffer_size_callback);
}
