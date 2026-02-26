#ifndef RENDERER_H
#define RENDERER_H

#include "map_data.h"

typedef struct {
    unsigned int program;
    int          mvp_loc;
    int          color_loc;

    /* Map coastline geometry */
    unsigned int map_vao;
    unsigned int map_vbo;
    int          map_segment_starts[MAX_SEGMENTS];
    int          map_segment_counts[MAX_SEGMENTS];
    int          map_num_segments;

    /* Country borders */
    unsigned int border_vao;
    unsigned int border_vbo;
    int          border_segment_starts[MAX_SEGMENTS];
    int          border_segment_counts[MAX_SEGMENTS];
    int          border_num_segments;

    /* Target line (center → target) */
    unsigned int line_vao;
    unsigned int line_vbo;

    /* Markers (center + target crosshairs) */
    unsigned int marker_vao;
    unsigned int marker_vbo;

    /* Earth boundary circle */
    unsigned int circle_vao;
    unsigned int circle_vbo;
    int          circle_vertex_count;

    /* Text overlay */
    unsigned int text_vao;
    unsigned int text_vbo;
    int          text_vertex_count;
} Renderer;

/* Initialize shaders and GL state. Returns 0 on success. */
int renderer_init(Renderer *r, const char *shader_dir);

/* Upload projected map data to GPU. */
void renderer_upload_map(Renderer *r, const MapData *md);

/* Upload country border data to GPU. */
void renderer_upload_borders(Renderer *r, const MapData *md);

/* Upload target line (two endpoints in km). */
void renderer_upload_target_line(Renderer *r, float cx, float cy, float tx, float ty);

/* Upload crosshair markers at center and target. */
void renderer_upload_markers(Renderer *r, float cx, float cy, float tx, float ty, float size_km);

/* Upload Earth boundary circle. */
void renderer_upload_earth_circle(Renderer *r);

/* Upload text overlay vertices (pixel-space GL_LINES). */
void renderer_upload_text(Renderer *r, float *verts, int vertex_count);

/* Draw everything. fb_w/fb_h needed for text overlay. */
void renderer_draw(const Renderer *r, const float *mvp, int fb_w, int fb_h);

/* Cleanup GL resources. */
void renderer_destroy(Renderer *r);

#endif
