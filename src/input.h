#ifndef INPUT_H
#define INPUT_H

#include <GLFW/glfw3.h>
#include "camera.h"

typedef struct {
    Camera *cam;
    int     dragging;
    double  last_mouse_x;
    double  last_mouse_y;
    int     win_width;
    int     win_height;
} InputState;

/* Initialize input state and install GLFW callbacks. */
void input_init(InputState *is, GLFWwindow *window, Camera *cam);

#endif
