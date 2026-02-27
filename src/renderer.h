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

    /* Land polygons (filled via stencil buffer) */
    unsigned int land_vao;
    unsigned int land_vbo;
    int          land_segment_starts[MAX_SEGMENTS];
    int          land_segment_counts[MAX_SEGMENTS];
    int          land_num_segments;

    /* Target line (great circle path, center → target) */
    unsigned int line_vao;
    unsigned int line_vbo;
    int          line_vertex_count;

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

    /* Earth filled disc (day-side base) */
    unsigned int disc_vao;
    unsigned int disc_vbo;
    int          disc_vertex_count;

    /* Grid (graticule) */
    unsigned int grid_vao;
    unsigned int grid_vbo;
    int          grid_segment_starts[MAX_SEGMENTS];
    int          grid_segment_counts[MAX_SEGMENTS];
    int          grid_num_segments;

    /* Night overlay (filled triangles with per-vertex alpha, km-space) */
    unsigned int night_vao;
    unsigned int night_vbo;
    int          night_vertex_count;

    /* Text overlay (pixel-space, HUD) */
    unsigned int text_vao;
    unsigned int text_vbo;
    int          text_vertex_count;

    /* Labels (pixel-space, positioned at marker screen coords) */
    unsigned int label_vao;
    unsigned int label_vbo;
    int          label_vertex_count;
    int          label_split;  /* vertex index where center label ends / target begins */

    /* Label backgrounds (pixel-space, semi-transparent quads behind labels) */
    unsigned int label_bg_vao;
    unsigned int label_bg_vbo;
    int          label_bg_split;  /* vertex index where center bg ends / target begins */
    int          label_bg_vertex_count;

    /* UI buttons (pixel-space) */
    unsigned int btn_bg_vao;
    unsigned int btn_bg_vbo;
    int          btn_bg_vertex_count;
    unsigned int btn_text_vao;
    unsigned int btn_text_vbo;
    int          btn_text_vertex_count;
    int          btn_count;        /* number of visible buttons */
    int          btn_hovered_quad; /* visible-button index of hovered button (-1 = none) */

    /* Popup panel (pixel-space) */
    unsigned int popup_bg_vao;
    unsigned int popup_bg_vbo;
    int          popup_bg_vertex_count;   /* GL_TRIANGLES */
    unsigned int popup_text_vao;
    unsigned int popup_text_vbo;
    int          popup_text_vertex_count; /* GL_LINES */
    int          popup_close_hovered;
} Renderer;

/* Initialize shaders and GL state. Returns 0 on success. */
int renderer_init(Renderer *r, const char *shader_dir);

/* Upload projected map data to GPU. */
void renderer_upload_map(Renderer *r, const MapData *md);

/* Upload country border data to GPU. */
void renderer_upload_borders(Renderer *r, const MapData *md);

/* Upload land polygon data to GPU (for stencil-based fill). */
void renderer_upload_land(Renderer *r, const MapData *md);

/* Upload target line vertices (great circle path in km-space). */
void renderer_upload_target_line(Renderer *r, const float *verts, int vertex_count);

/* Upload markers: filled circle at center, outline circle at target. */
void renderer_upload_markers(Renderer *r, float cx, float cy, float tx, float ty, float size_km);

/* Upload north pole triangle marker (km-space position + size). */
void renderer_upload_npole(Renderer *r, float px, float py, float size_km);

/* Upload Earth boundary circle and filled disc for the given radius. */
void renderer_upload_earth_circle(Renderer *r, double radius);

/* Upload grid (graticule) data to GPU. */
void renderer_upload_grid(Renderer *r, const MapData *md);

/* Upload night overlay mesh (GL_TRIANGLES, 3 floats per vertex: x, y, alpha). */
void renderer_upload_night(Renderer *r, const float *vertices, int vertex_count);

/* Upload text overlay vertices (pixel-space GL_LINES). */
void renderer_upload_text(Renderer *r, float *verts, int vertex_count);

/* Upload label vertices (pixel-space GL_LINES).
 * split: vertex index where center label ends and target label begins. */
void renderer_upload_labels(Renderer *r, float *verts, int vertex_count, int split);

/* Upload label background quads (pixel-space GL_TRIANGLES).
 * split: vertex index where center bg ends and target bg begins. */
void renderer_upload_label_bgs(Renderer *r, float *verts, int vertex_count, int split);

/* Upload UI button geometry (pixel-space).
 * quad_verts: GL_TRIANGLES background quads, text_verts: GL_LINES label text.
 * btn_count: number of visible buttons, hovered_quad: which one is hovered (-1 = none). */
void renderer_upload_buttons(Renderer *r,
                             float *quad_verts, int quad_vert_count,
                             float *text_verts, int text_vert_count,
                             int btn_count, int hovered_quad);

/* Upload popup panel geometry (pixel-space).
 * quad_verts: 3 quads (body, title bar, close btn) as GL_TRIANGLES.
 * text_verts: title + "X" as GL_LINES. close_hovered: highlight close btn. */
void renderer_upload_popup(Renderer *r,
                           float *quad_verts, int quad_vert_count,
                           float *text_verts, int text_vert_count,
                           int close_hovered);

/* Draw everything. fb_w/fb_h needed for text overlay. */
void renderer_draw(const Renderer *r, const float *mvp, int fb_w, int fb_h);

/* Cleanup GL resources. */
void renderer_destroy(Renderer *r);

#endif
