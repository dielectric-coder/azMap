#ifndef CAMERA_H
#define CAMERA_H

#define ZOOM_MIN_KM     10.0f      /* Max zoom in: 10 km diameter */
#define ZOOM_MAX_KM     40030.0f   /* Min zoom: full Earth circumference */
#define ZOOM_DEFAULT_KM 40030.0f

typedef struct {
    float zoom_km;   /* Visible diameter in km */
    float pan_x;     /* Pan offset in km */
    float pan_y;     /* Pan offset in km */
    float aspect;    /* Window width / height */
} Camera;

void camera_init(Camera *cam);

/* Zoom by a multiplicative factor (>1 zooms out, <1 zooms in). */
void camera_zoom(Camera *cam, float factor);

/* Pan by dx, dy in km. */
void camera_pan(Camera *cam, float dx_km, float dy_km);

/* Reset to default view. */
void camera_reset(Camera *cam);

/* Compute a 4x4 orthographic MVP matrix (column-major, for OpenGL). */
void camera_get_mvp(const Camera *cam, float *mat4);

#endif
