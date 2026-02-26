#include "camera.h"
#include <string.h>

void camera_init(Camera *cam)
{
    cam->zoom_km = ZOOM_DEFAULT_KM;
    cam->pan_x = 0.0f;
    cam->pan_y = 0.0f;
    cam->aspect = 1.0f;
}

void camera_zoom(Camera *cam, float factor)
{
    cam->zoom_km *= factor;
    if (cam->zoom_km < ZOOM_MIN_KM) cam->zoom_km = ZOOM_MIN_KM;
    if (cam->zoom_km > ZOOM_MAX_KM) cam->zoom_km = ZOOM_MAX_KM;
}

void camera_pan(Camera *cam, float dx_km, float dy_km)
{
    cam->pan_x += dx_km;
    cam->pan_y += dy_km;
}

void camera_reset(Camera *cam)
{
    cam->zoom_km = ZOOM_DEFAULT_KM;
    cam->pan_x = 0.0f;
    cam->pan_y = 0.0f;
}

void camera_get_mvp(const Camera *cam, float *m)
{
    /* Orthographic projection: maps km-space to clip [-1,1].
     * half_h = zoom_km / 2, half_w = half_h * aspect.
     * Center shifted by pan_x, pan_y. */
    float half_h = cam->zoom_km * 0.5f;
    float half_w = half_h * cam->aspect;

    float left   = cam->pan_x - half_w;
    float right  = cam->pan_x + half_w;
    float bottom = cam->pan_y - half_h;
    float top    = cam->pan_y + half_h;

    /* Column-major 4x4 ortho matrix */
    memset(m, 0, 16 * sizeof(float));
    m[0]  = 2.0f / (right - left);
    m[5]  = 2.0f / (top - bottom);
    m[10] = -1.0f;
    m[12] = -(right + left) / (right - left);
    m[13] = -(top + bottom) / (top - bottom);
    m[15] = 1.0f;
}
