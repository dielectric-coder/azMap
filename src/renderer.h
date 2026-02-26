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

    /* Center marker (filled circle — GL_TRIANGLE_FAN) */
    unsigned int center_marker_vao;
    unsigned int center_marker_vbo;
    int          center_marker_vcount;

    /* Target marker (outline circle — GL_LINE_LOOP) */
    unsigned int target_marker_vao;
    unsigned int target_marker_vbo;
    int          target_marker_vcount;

    /* North pole triangle (filled) */
    unsigned int npole_vao;
    unsigned int npole_vbo;

    /* Earth boundary circle */
    unsigned int circle_vao;
    unsigned int circle_vbo;
    int          circle_vertex_count;

    /* Grid (graticule) */
    unsigned int grid_vao;
    unsigned int grid_vbo;
    int          grid_segment_starts[MAX_SEGMENTS];
    int          grid_segment_counts[MAX_SEGMENTS];
    int          grid_num_segments;

    /* Text overlay (pixel-space, HUD) */
    unsigned int text_vao;
    unsigned int text_vbo;
    int          text_vertex_count;

    /* Labels (pixel-space, positioned at marker screen coords) */
    unsigned int label_vao;
    unsigned int label_vbo;
    int          label_vertex_count;
    int          label_split;  /* vertex index where center label ends / target begins */
} Renderer;

/* Initialize shaders and GL state. Returns 0 on success. */
int renderer_init(Renderer *r, const char *shader_dir);

/* Upload projected map data to GPU. */
void renderer_upload_map(Renderer *r, const MapData *md);

/* Upload country border data to GPU. */
void renderer_upload_borders(Renderer *r, const MapData *md);

/* Upload target line (two endpoints in km). */
void renderer_upload_target_line(Renderer *r, float cx, float cy, float tx, float ty);

/* Upload markers: filled circle at center, outline circle at target. */
void renderer_upload_markers(Renderer *r, float cx, float cy, float tx, float ty, float size_km);

/* Upload north pole triangle marker (km-space position + size). */
void renderer_upload_npole(Renderer *r, float px, float py, float size_km);

/* Upload Earth boundary circle. */
void renderer_upload_earth_circle(Renderer *r);

/* Upload grid (graticule) data to GPU. */
void renderer_upload_grid(Renderer *r, const MapData *md);

/* Upload text overlay vertices (pixel-space GL_LINES). */
void renderer_upload_text(Renderer *r, float *verts, int vertex_count);

/* Upload label vertices (pixel-space GL_LINES).
 * split: vertex index where center label ends and target label begins. */
void renderer_upload_labels(Renderer *r, float *verts, int vertex_count, int split);

/* Draw everything. fb_w/fb_h needed for text overlay. */
void renderer_draw(const Renderer *r, const float *mvp, int fb_w, int fb_h);

/* Cleanup GL resources. */
void renderer_destroy(Renderer *r);

#endif
