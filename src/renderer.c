#include <GL/glew.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "renderer.h"
#include "projection.h"

static char *read_file(const char *path)
{
    FILE *f = fopen(path, "rb");
    if (!f) {
        fprintf(stderr, "Error: cannot open shader: %s\n", path);
        return NULL;
    }
    fseek(f, 0, SEEK_END);
    long len = ftell(f);
    fseek(f, 0, SEEK_SET);
    char *buf = malloc(len + 1);
    fread(buf, 1, len, f);
    buf[len] = '\0';
    fclose(f);
    return buf;
}

static unsigned int compile_shader(const char *src, GLenum type)
{
    unsigned int s = glCreateShader(type);
    glShaderSource(s, 1, (const char **)&src, NULL);
    glCompileShader(s);
    int ok;
    glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[512];
        glGetShaderInfoLog(s, sizeof(log), NULL, log);
        fprintf(stderr, "Shader compile error: %s\n", log);
    }
    return s;
}

int renderer_init(Renderer *r, const char *shader_dir)
{
    memset(r, 0, sizeof(*r));

    char vert_path[512], frag_path[512];
    snprintf(vert_path, sizeof(vert_path), "%s/map.vert", shader_dir);
    snprintf(frag_path, sizeof(frag_path), "%s/map.frag", shader_dir);

    char *vert_src = read_file(vert_path);
    char *frag_src = read_file(frag_path);
    if (!vert_src || !frag_src) {
        free(vert_src);
        free(frag_src);
        return -1;
    }

    unsigned int vs = compile_shader(vert_src, GL_VERTEX_SHADER);
    unsigned int fs = compile_shader(frag_src, GL_FRAGMENT_SHADER);
    free(vert_src);
    free(frag_src);

    r->program = glCreateProgram();
    glAttachShader(r->program, vs);
    glAttachShader(r->program, fs);
    glLinkProgram(r->program);

    glDeleteShader(vs);
    glDeleteShader(fs);

    int ok;
    glGetProgramiv(r->program, GL_LINK_STATUS, &ok);
    if (!ok) {
        char log[512];
        glGetProgramInfoLog(r->program, sizeof(log), NULL, log);
        fprintf(stderr, "Program link error: %s\n", log);
        return -1;
    }

    r->mvp_loc = glGetUniformLocation(r->program, "u_mvp");
    r->color_loc = glGetUniformLocation(r->program, "u_color");

    /* GL state */
    glEnable(GL_LINE_SMOOTH);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glLineWidth(1.5f);
    glClearColor(0.05f, 0.05f, 0.12f, 1.0f);

    return 0;
}

void renderer_upload_map(Renderer *r, const MapData *md)
{
    if (!r->map_vao) {
        glGenVertexArrays(1, &r->map_vao);
        glGenBuffers(1, &r->map_vbo);
    }
    glBindVertexArray(r->map_vao);
    glBindBuffer(GL_ARRAY_BUFFER, r->map_vbo);
    glBufferData(GL_ARRAY_BUFFER, md->vertex_count * 2 * sizeof(float),
                 md->vertices, GL_DYNAMIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, NULL);
    glBindVertexArray(0);

    r->map_num_segments = md->num_segments;
    for (int i = 0; i < md->num_segments; i++) {
        r->map_segment_starts[i] = md->segment_starts[i];
        r->map_segment_counts[i] = md->segment_counts[i];
    }
}

void renderer_upload_borders(Renderer *r, const MapData *md)
{
    if (!r->border_vao) {
        glGenVertexArrays(1, &r->border_vao);
        glGenBuffers(1, &r->border_vbo);
    }
    glBindVertexArray(r->border_vao);
    glBindBuffer(GL_ARRAY_BUFFER, r->border_vbo);
    glBufferData(GL_ARRAY_BUFFER, md->vertex_count * 2 * sizeof(float),
                 md->vertices, GL_DYNAMIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, NULL);
    glBindVertexArray(0);

    r->border_num_segments = md->num_segments;
    for (int i = 0; i < md->num_segments; i++) {
        r->border_segment_starts[i] = md->segment_starts[i];
        r->border_segment_counts[i] = md->segment_counts[i];
    }
}

void renderer_upload_land(Renderer *r, const MapData *md)
{
    if (!r->land_vao) {
        glGenVertexArrays(1, &r->land_vao);
        glGenBuffers(1, &r->land_vbo);
    }
    glBindVertexArray(r->land_vao);
    glBindBuffer(GL_ARRAY_BUFFER, r->land_vbo);
    glBufferData(GL_ARRAY_BUFFER, md->vertex_count * 2 * sizeof(float),
                 md->vertices, GL_DYNAMIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, NULL);
    glBindVertexArray(0);

    r->land_num_segments = md->num_segments;
    for (int i = 0; i < md->num_segments; i++) {
        r->land_segment_starts[i] = md->segment_starts[i];
        r->land_segment_counts[i] = md->segment_counts[i];
    }
}

void renderer_upload_target_line(Renderer *r, const float *verts, int vertex_count)
{
    if (!r->line_vao) {
        glGenVertexArrays(1, &r->line_vao);
        glGenBuffers(1, &r->line_vbo);
    }
    glBindVertexArray(r->line_vao);
    glBindBuffer(GL_ARRAY_BUFFER, r->line_vbo);
    glBufferData(GL_ARRAY_BUFFER, vertex_count * 2 * sizeof(float), verts, GL_DYNAMIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, NULL);
    glBindVertexArray(0);
    r->line_vertex_count = vertex_count;
}

void renderer_upload_markers(Renderer *r, float cx, float cy, float tx, float ty, float size_km)
{
    #define MARKER_SEGS 32
    float s = size_km;

    /* Center: filled circle (triangle fan: center + ring + closing vertex) */
    {
        int n = MARKER_SEGS + 2;
        float verts[(MARKER_SEGS + 2) * 2];
        verts[0] = cx;
        verts[1] = cy;
        for (int i = 0; i <= MARKER_SEGS; i++) {
            float a = 2.0f * (float)M_PI * i / MARKER_SEGS;
            verts[(i + 1) * 2]     = cx + s * cosf(a);
            verts[(i + 1) * 2 + 1] = cy + s * sinf(a);
        }
        if (!r->center_marker_vao) {
            glGenVertexArrays(1, &r->center_marker_vao);
            glGenBuffers(1, &r->center_marker_vbo);
        }
        glBindVertexArray(r->center_marker_vao);
        glBindBuffer(GL_ARRAY_BUFFER, r->center_marker_vbo);
        glBufferData(GL_ARRAY_BUFFER, sizeof(float) * n * 2, verts, GL_DYNAMIC_DRAW);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, NULL);
        glBindVertexArray(0);
        r->center_marker_vcount = n;
    }

    /* Target: outline circle (line loop) */
    {
        float verts[MARKER_SEGS * 2];
        for (int i = 0; i < MARKER_SEGS; i++) {
            float a = 2.0f * (float)M_PI * i / MARKER_SEGS;
            verts[i * 2]     = tx + s * cosf(a);
            verts[i * 2 + 1] = ty + s * sinf(a);
        }
        if (!r->target_marker_vao) {
            glGenVertexArrays(1, &r->target_marker_vao);
            glGenBuffers(1, &r->target_marker_vbo);
        }
        glBindVertexArray(r->target_marker_vao);
        glBindBuffer(GL_ARRAY_BUFFER, r->target_marker_vbo);
        glBufferData(GL_ARRAY_BUFFER, sizeof(float) * MARKER_SEGS * 2, verts, GL_DYNAMIC_DRAW);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, NULL);
        glBindVertexArray(0);
        r->target_marker_vcount = MARKER_SEGS;
    }
    #undef MARKER_SEGS
}

void renderer_upload_npole(Renderer *r, float px, float py, float size_km)
{
    float s = size_km;
    /* Upward-pointing triangle */
    float verts[] = {
        px,         py - s,       /* top */
        px - s * 0.866f, py + s * 0.5f, /* bottom-left */
        px + s * 0.866f, py + s * 0.5f, /* bottom-right */
    };
    if (!r->npole_vao) {
        glGenVertexArrays(1, &r->npole_vao);
        glGenBuffers(1, &r->npole_vbo);
    }
    glBindVertexArray(r->npole_vao);
    glBindBuffer(GL_ARRAY_BUFFER, r->npole_vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(verts), verts, GL_DYNAMIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, NULL);
    glBindVertexArray(0);
}

void renderer_upload_earth_circle(Renderer *r, double radius)
{
    int n = 360;
    float *verts = malloc(n * 2 * sizeof(float));
    for (int i = 0; i < n; i++) {
        double a = 2.0 * M_PI * i / n;
        verts[i * 2]     = (float)(radius * cos(a));
        verts[i * 2 + 1] = (float)(radius * sin(a));
    }
    if (!r->circle_vao) {
        glGenVertexArrays(1, &r->circle_vao);
        glGenBuffers(1, &r->circle_vbo);
    }
    glBindVertexArray(r->circle_vao);
    glBindBuffer(GL_ARRAY_BUFFER, r->circle_vbo);
    glBufferData(GL_ARRAY_BUFFER, n * 2 * sizeof(float), verts, GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, NULL);
    glBindVertexArray(0);
    r->circle_vertex_count = n;

    /* Filled disc (TRIANGLE_FAN: center + ring) */
    int dn = n + 2;  /* center + n ring points + closing point */
    float *dverts = malloc(dn * 2 * sizeof(float));
    dverts[0] = 0.0f;
    dverts[1] = 0.0f;
    for (int i = 0; i <= n; i++) {
        double a = 2.0 * M_PI * (i % n) / n;
        dverts[(i + 1) * 2]     = (float)(radius * cos(a));
        dverts[(i + 1) * 2 + 1] = (float)(radius * sin(a));
    }
    if (!r->disc_vao) {
        glGenVertexArrays(1, &r->disc_vao);
        glGenBuffers(1, &r->disc_vbo);
    }
    glBindVertexArray(r->disc_vao);
    glBindBuffer(GL_ARRAY_BUFFER, r->disc_vbo);
    glBufferData(GL_ARRAY_BUFFER, dn * 2 * sizeof(float), dverts, GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, NULL);
    glBindVertexArray(0);
    r->disc_vertex_count = dn;
    free(dverts);

    free(verts);
}

void renderer_upload_grid(Renderer *r, const MapData *md)
{
    if (!r->grid_vao) {
        glGenVertexArrays(1, &r->grid_vao);
        glGenBuffers(1, &r->grid_vbo);
    }
    glBindVertexArray(r->grid_vao);
    glBindBuffer(GL_ARRAY_BUFFER, r->grid_vbo);
    glBufferData(GL_ARRAY_BUFFER, md->vertex_count * 2 * sizeof(float),
                 md->vertices, GL_DYNAMIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, NULL);
    glBindVertexArray(0);

    r->grid_num_segments = md->num_segments;
    for (int i = 0; i < md->num_segments; i++) {
        r->grid_segment_starts[i] = md->segment_starts[i];
        r->grid_segment_counts[i] = md->segment_counts[i];
    }
}

void renderer_upload_night(Renderer *r, const float *vertices, int vertex_count)
{
    if (!r->night_vao) {
        glGenVertexArrays(1, &r->night_vao);
        glGenBuffers(1, &r->night_vbo);
    }
    glBindVertexArray(r->night_vao);
    glBindBuffer(GL_ARRAY_BUFFER, r->night_vbo);
    glBufferData(GL_ARRAY_BUFFER, vertex_count * 3 * sizeof(float),
                 vertices, GL_DYNAMIC_DRAW);
    /* attribute 0: position (x, y) */
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 3 * sizeof(float), (void *)0);
    /* attribute 1: alpha */
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 1, GL_FLOAT, GL_FALSE, 3 * sizeof(float),
                          (void *)(2 * sizeof(float)));
    glBindVertexArray(0);
    r->night_vertex_count = vertex_count;
}

void renderer_upload_labels(Renderer *r, float *verts, int vertex_count, int split)
{
    if (!r->label_vao) {
        glGenVertexArrays(1, &r->label_vao);
        glGenBuffers(1, &r->label_vbo);
    }
    glBindVertexArray(r->label_vao);
    glBindBuffer(GL_ARRAY_BUFFER, r->label_vbo);
    glBufferData(GL_ARRAY_BUFFER, vertex_count * 2 * sizeof(float),
                 verts, GL_DYNAMIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, NULL);
    glBindVertexArray(0);
    r->label_vertex_count = vertex_count;
    r->label_split = split;
}

void renderer_upload_label_bgs(Renderer *r, float *verts, int vertex_count, int split)
{
    if (!r->label_bg_vao) {
        glGenVertexArrays(1, &r->label_bg_vao);
        glGenBuffers(1, &r->label_bg_vbo);
    }
    glBindVertexArray(r->label_bg_vao);
    glBindBuffer(GL_ARRAY_BUFFER, r->label_bg_vbo);
    glBufferData(GL_ARRAY_BUFFER, vertex_count * 2 * sizeof(float),
                 verts, GL_DYNAMIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, NULL);
    glBindVertexArray(0);
    r->label_bg_vertex_count = vertex_count;
    r->label_bg_split = split;
}

void renderer_upload_buttons(Renderer *r,
                             float *quad_verts, int quad_vert_count,
                             float *text_verts, int text_vert_count,
                             int btn_count, int hovered_quad)
{
    /* Background quads */
    if (!r->btn_bg_vao) {
        glGenVertexArrays(1, &r->btn_bg_vao);
        glGenBuffers(1, &r->btn_bg_vbo);
    }
    glBindVertexArray(r->btn_bg_vao);
    glBindBuffer(GL_ARRAY_BUFFER, r->btn_bg_vbo);
    glBufferData(GL_ARRAY_BUFFER, quad_vert_count * 2 * sizeof(float),
                 quad_verts, GL_DYNAMIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, NULL);
    glBindVertexArray(0);
    r->btn_bg_vertex_count = quad_vert_count;

    /* Text */
    if (!r->btn_text_vao) {
        glGenVertexArrays(1, &r->btn_text_vao);
        glGenBuffers(1, &r->btn_text_vbo);
    }
    glBindVertexArray(r->btn_text_vao);
    glBindBuffer(GL_ARRAY_BUFFER, r->btn_text_vbo);
    glBufferData(GL_ARRAY_BUFFER, text_vert_count * 2 * sizeof(float),
                 text_verts, GL_DYNAMIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, NULL);
    glBindVertexArray(0);
    r->btn_text_vertex_count = text_vert_count;

    r->btn_count = btn_count;
    r->btn_hovered_quad = hovered_quad;
}

void renderer_upload_popup(Renderer *r,
                           float *quad_verts, int quad_vert_count,
                           float *text_verts, int text_vert_count,
                           int close_hovered)
{
    if (!r->popup_bg_vao) {
        glGenVertexArrays(1, &r->popup_bg_vao);
        glGenBuffers(1, &r->popup_bg_vbo);
    }
    glBindVertexArray(r->popup_bg_vao);
    glBindBuffer(GL_ARRAY_BUFFER, r->popup_bg_vbo);
    glBufferData(GL_ARRAY_BUFFER, quad_vert_count * 2 * sizeof(float),
                 quad_verts, GL_DYNAMIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, NULL);
    glBindVertexArray(0);
    r->popup_bg_vertex_count = quad_vert_count;

    if (!r->popup_text_vao) {
        glGenVertexArrays(1, &r->popup_text_vao);
        glGenBuffers(1, &r->popup_text_vbo);
    }
    glBindVertexArray(r->popup_text_vao);
    glBindBuffer(GL_ARRAY_BUFFER, r->popup_text_vbo);
    glBufferData(GL_ARRAY_BUFFER, text_vert_count * 2 * sizeof(float),
                 text_verts, GL_DYNAMIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, NULL);
    glBindVertexArray(0);
    r->popup_text_vertex_count = text_vert_count;

    r->popup_close_hovered = close_hovered;
}

void renderer_upload_text(Renderer *r, float *verts, int vertex_count)
{
    if (!r->text_vao) {
        glGenVertexArrays(1, &r->text_vao);
        glGenBuffers(1, &r->text_vbo);
    }
    glBindVertexArray(r->text_vao);
    glBindBuffer(GL_ARRAY_BUFFER, r->text_vbo);
    glBufferData(GL_ARRAY_BUFFER, vertex_count * 2 * sizeof(float),
                 verts, GL_DYNAMIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, NULL);
    glBindVertexArray(0);
    r->text_vertex_count = vertex_count;
}

void renderer_draw(const Renderer *r, const float *mvp, int fb_w, int fb_h)
{
    glClear(GL_COLOR_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);
    glUseProgram(r->program);
    glUniformMatrix4fv(r->mvp_loc, 1, GL_FALSE, mvp);

    /* Default vertex alpha = 1.0 for all geometry without per-vertex alpha */
    glVertexAttrib1f(1, 1.0f);

    /* Earth filled disc - slightly lighter base for day/night contrast */
    if (r->disc_vao) {
        glUniform4f(r->color_loc, 0.12f, 0.12f, 0.25f, 1.0f);
        glBindVertexArray(r->disc_vao);
        glDrawArrays(GL_TRIANGLE_FAN, 0, r->disc_vertex_count);
    }

    /* Land fill via stencil buffer (odd-even rule, clipped to disc).
     * Uses stencil bit 7 to mask the disc area so back-hemisphere
     * vertices (projected to 1e6) don't corrupt the stencil. */
    if (r->land_vao && r->land_num_segments > 0 && r->disc_vao) {
        glEnable(GL_STENCIL_TEST);
        glColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);

        /* Step 1: mark disc area in stencil bit 7 */
        glStencilMask(0x80);
        glStencilFunc(GL_ALWAYS, 0x80, 0x80);
        glStencilOp(GL_KEEP, GL_KEEP, GL_REPLACE);
        glBindVertexArray(r->disc_vao);
        glDrawArrays(GL_TRIANGLE_FAN, 0, r->disc_vertex_count);

        /* Step 2: draw land rings with INVERT on lower bits, only inside disc */
        glStencilMask(0x7F);
        glStencilFunc(GL_EQUAL, 0x80, 0x80);
        glStencilOp(GL_KEEP, GL_KEEP, GL_INVERT);
        glBindVertexArray(r->land_vao);
        for (int i = 0; i < r->land_num_segments; i++) {
            glDrawArrays(GL_TRIANGLE_FAN,
                         r->land_segment_starts[i],
                         r->land_segment_counts[i]);
        }

        /* Step 3: draw land color where disc+land bits are set (stencil > 0x80) */
        glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
        glStencilMask(0x00);
        glStencilFunc(GL_LESS, 0x80, 0xFF);
        glStencilOp(GL_KEEP, GL_KEEP, GL_KEEP);
        glUniform4f(r->color_loc, 0.12f, 0.15f, 0.10f, 1.0f);
        glBindVertexArray(r->disc_vao);
        glDrawArrays(GL_TRIANGLE_FAN, 0, r->disc_vertex_count);

        glStencilMask(0xFF);
        glDisable(GL_STENCIL_TEST);
    }

    /* Earth boundary circle - dark blue */
    if (r->circle_vao) {
        glUniform4f(r->color_loc, 0.15f, 0.15f, 0.3f, 1.0f);
        glBindVertexArray(r->circle_vao);
        glDrawArrays(GL_LINE_LOOP, 0, r->circle_vertex_count);
    }

    /* Grid (graticule) - very dim */
    if (r->grid_vao) {
        glUniform4f(r->color_loc, 0.2f, 0.2f, 0.3f, 1.0f);
        glBindVertexArray(r->grid_vao);
        for (int i = 0; i < r->grid_num_segments; i++) {
            glDrawArrays(GL_LINE_STRIP,
                         r->grid_segment_starts[i],
                         r->grid_segment_counts[i]);
        }
    }

    /* Night overlay - smooth gradient via per-vertex alpha */
    if (r->night_vao && r->night_vertex_count > 0) {
        glUniform4f(r->color_loc, 0.0f, 0.0f, 0.05f, 1.0f);
        glBindVertexArray(r->night_vao);
        glDrawArrays(GL_TRIANGLES, 0, r->night_vertex_count);
    }

    /* Country borders - dim gray */
    if (r->border_vao) {
        glUniform4f(r->color_loc, 0.4f, 0.4f, 0.5f, 1.0f);
        glBindVertexArray(r->border_vao);
        for (int i = 0; i < r->border_num_segments; i++) {
            glDrawArrays(GL_LINE_STRIP,
                         r->border_segment_starts[i],
                         r->border_segment_counts[i]);
        }
    }

    /* Coastlines - green */
    if (r->map_vao) {
        glUniform4f(r->color_loc, 0.2f, 0.8f, 0.3f, 1.0f);
        glBindVertexArray(r->map_vao);
        for (int i = 0; i < r->map_num_segments; i++) {
            glDrawArrays(GL_LINE_STRIP,
                         r->map_segment_starts[i],
                         r->map_segment_counts[i]);
        }
    }

    /* Target line - yellow (great circle path) */
    if (r->line_vao && r->line_vertex_count > 1) {
        glUniform4f(r->color_loc, 1.0f, 0.9f, 0.2f, 1.0f);
        glBindVertexArray(r->line_vao);
        glDrawArrays(GL_LINE_STRIP, 0, r->line_vertex_count);
    }

    /* Center marker - white filled circle */
    if (r->center_marker_vao) {
        glUniform4f(r->color_loc, 1.0f, 1.0f, 1.0f, 1.0f);
        glBindVertexArray(r->center_marker_vao);
        glDrawArrays(GL_TRIANGLE_FAN, 0, r->center_marker_vcount);
    }

    /* Target marker - red outline circle */
    if (r->target_marker_vao) {
        glUniform4f(r->color_loc, 1.0f, 0.3f, 0.2f, 1.0f);
        glBindVertexArray(r->target_marker_vao);
        glDrawArrays(GL_LINE_LOOP, 0, r->target_marker_vcount);
    }

    /* North pole triangle - white */
    if (r->npole_vao) {
        glUniform4f(r->color_loc, 1.0f, 1.0f, 1.0f, 1.0f);
        glBindVertexArray(r->npole_vao);
        glDrawArrays(GL_TRIANGLES, 0, 3);
    }

    /* Pixel-space overlays — switch to pixel-space orthographic matrix */
    if (fb_w > 0 && fb_h > 0) {
        float ortho[16];
        memset(ortho, 0, sizeof(ortho));
        ortho[0]  = 2.0f / (float)fb_w;
        ortho[5]  = -2.0f / (float)fb_h;  /* y down */
        ortho[10] = -1.0f;
        ortho[12] = -1.0f;
        ortho[13] = 1.0f;
        ortho[15] = 1.0f;
        glUniformMatrix4fv(r->mvp_loc, 1, GL_FALSE, ortho);

        /* Label backgrounds (semi-transparent) */
        if (r->label_bg_vao && r->label_bg_vertex_count > 0) {
            glBindVertexArray(r->label_bg_vao);
            if (r->label_bg_split > 0) {
                glUniform4f(r->color_loc, 0.0f, 0.0f, 0.0f, 0.55f);
                glDrawArrays(GL_TRIANGLES, 0, r->label_bg_split);
            }
            int bg_target = r->label_bg_vertex_count - r->label_bg_split;
            if (bg_target > 0) {
                glUniform4f(r->color_loc, 0.0f, 0.0f, 0.0f, 0.55f);
                glDrawArrays(GL_TRIANGLES, r->label_bg_split, bg_target);
            }
        }

        /* Labels (center = cyan, target = orange) */
        if (r->label_vao && r->label_vertex_count > 0) {
            glBindVertexArray(r->label_vao);
            if (r->label_split > 0) {
                glUniform4f(r->color_loc, 0.3f, 1.0f, 1.0f, 1.0f);
                glDrawArrays(GL_LINES, 0, r->label_split);
            }
            int target_count = r->label_vertex_count - r->label_split;
            if (target_count > 0) {
                glUniform4f(r->color_loc, 1.0f, 0.6f, 0.2f, 1.0f);
                glDrawArrays(GL_LINES, r->label_split, target_count);
            }
        }

        /* UI button backgrounds */
        if (r->btn_bg_vao && r->btn_bg_vertex_count > 0) {
            glBindVertexArray(r->btn_bg_vao);
            for (int i = 0; i < r->btn_count; i++) {
                if (i == r->btn_hovered_quad)
                    glUniform4f(r->color_loc, 0.25f, 0.25f, 0.35f, 0.75f);
                else
                    glUniform4f(r->color_loc, 0.1f, 0.1f, 0.18f, 0.65f);
                glDrawArrays(GL_TRIANGLES, i * 6, 6);
            }
        }

        /* UI button text */
        if (r->btn_text_vao && r->btn_text_vertex_count > 0) {
            glUniform4f(r->color_loc, 1.0f, 1.0f, 1.0f, 1.0f);
            glBindVertexArray(r->btn_text_vao);
            glDrawArrays(GL_LINES, 0, r->btn_text_vertex_count);
        }

        /* Popup panel */
        if (r->popup_bg_vao && r->popup_bg_vertex_count > 0) {
            glBindVertexArray(r->popup_bg_vao);
            /* Quad 0: body (dark) */
            glUniform4f(r->color_loc, 0.08f, 0.08f, 0.14f, 0.90f);
            glDrawArrays(GL_TRIANGLES, 0, 6);
            /* Quad 1: title bar (slightly lighter) */
            glUniform4f(r->color_loc, 0.15f, 0.15f, 0.25f, 0.92f);
            glDrawArrays(GL_TRIANGLES, 6, 6);
            /* Quad 2: close button */
            if (r->popup_close_hovered)
                glUniform4f(r->color_loc, 0.4f, 0.15f, 0.15f, 0.92f);
            else
                glUniform4f(r->color_loc, 0.25f, 0.12f, 0.12f, 0.92f);
            glDrawArrays(GL_TRIANGLES, 12, 6);
            /* Quad 3: input box (if present) */
            if (r->popup_bg_vertex_count > 18) {
                glUniform4f(r->color_loc, 0.04f, 0.04f, 0.08f, 0.95f);
                glDrawArrays(GL_TRIANGLES, 18, 6);
            }
        }

        /* Popup text */
        if (r->popup_text_vao && r->popup_text_vertex_count > 0) {
            glUniform4f(r->color_loc, 1.0f, 1.0f, 1.0f, 1.0f);
            glBindVertexArray(r->popup_text_vao);
            glDrawArrays(GL_LINES, 0, r->popup_text_vertex_count);
        }

        /* HUD text overlay */
        if (r->text_vao && r->text_vertex_count > 0) {
            glUniform4f(r->color_loc, 1.0f, 1.0f, 1.0f, 1.0f);
            glBindVertexArray(r->text_vao);
            glDrawArrays(GL_LINES, 0, r->text_vertex_count);
        }
    }

    glBindVertexArray(0);
}

void renderer_destroy(Renderer *r)
{
    glDeleteProgram(r->program);
    if (r->map_vao) { glDeleteVertexArrays(1, &r->map_vao); glDeleteBuffers(1, &r->map_vbo); }
    if (r->border_vao) { glDeleteVertexArrays(1, &r->border_vao); glDeleteBuffers(1, &r->border_vbo); }
    if (r->land_vao) { glDeleteVertexArrays(1, &r->land_vao); glDeleteBuffers(1, &r->land_vbo); }
    if (r->line_vao) { glDeleteVertexArrays(1, &r->line_vao); glDeleteBuffers(1, &r->line_vbo); }
    if (r->npole_vao) { glDeleteVertexArrays(1, &r->npole_vao); glDeleteBuffers(1, &r->npole_vbo); }
    if (r->center_marker_vao) { glDeleteVertexArrays(1, &r->center_marker_vao); glDeleteBuffers(1, &r->center_marker_vbo); }
    if (r->target_marker_vao) { glDeleteVertexArrays(1, &r->target_marker_vao); glDeleteBuffers(1, &r->target_marker_vbo); }
    if (r->circle_vao) { glDeleteVertexArrays(1, &r->circle_vao); glDeleteBuffers(1, &r->circle_vbo); }
    if (r->disc_vao) { glDeleteVertexArrays(1, &r->disc_vao); glDeleteBuffers(1, &r->disc_vbo); }
    if (r->grid_vao) { glDeleteVertexArrays(1, &r->grid_vao); glDeleteBuffers(1, &r->grid_vbo); }
    if (r->night_vao) { glDeleteVertexArrays(1, &r->night_vao); glDeleteBuffers(1, &r->night_vbo); }
    if (r->text_vao) { glDeleteVertexArrays(1, &r->text_vao); glDeleteBuffers(1, &r->text_vbo); }
    if (r->label_vao) { glDeleteVertexArrays(1, &r->label_vao); glDeleteBuffers(1, &r->label_vbo); }
    if (r->label_bg_vao) { glDeleteVertexArrays(1, &r->label_bg_vao); glDeleteBuffers(1, &r->label_bg_vbo); }
    if (r->btn_bg_vao) { glDeleteVertexArrays(1, &r->btn_bg_vao); glDeleteBuffers(1, &r->btn_bg_vbo); }
    if (r->btn_text_vao) { glDeleteVertexArrays(1, &r->btn_text_vao); glDeleteBuffers(1, &r->btn_text_vbo); }
    if (r->popup_bg_vao) { glDeleteVertexArrays(1, &r->popup_bg_vao); glDeleteBuffers(1, &r->popup_bg_vbo); }
    if (r->popup_text_vao) { glDeleteVertexArrays(1, &r->popup_text_vao); glDeleteBuffers(1, &r->popup_text_vbo); }
}
