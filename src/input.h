#ifndef INPUT_H
#define INPUT_H

#include <GLFW/glfw3.h>
#include "camera.h"
#include "ui.h"

typedef struct {
    Camera *cam;
    UI     *ui;
    int     dragging;
    int     popup_dragging;  /* dragging popup title bar */
    int     pressed;         /* left button currently held */
    double  press_x;         /* cursor pos at press (screen coords) */
    double  press_y;
    double  last_mouse_x;
    double  last_mouse_y;
    int     win_width;
    int     win_height;
    float   cursor_scale_x;  /* framebuffer / window scale */
    float   cursor_scale_y;
    double  center_lat, center_lon;           /* current projection center */
    double  original_center_lat, original_center_lon; /* for R reset */
    int     center_dirty;                     /* set by drag/keys, cleared by main */
} InputState;

/* Initialize input state and install GLFW callbacks. */
void input_init(InputState *is, GLFWwindow *window, Camera *cam, UI *ui,
                double center_lat, double center_lon);

#endif
