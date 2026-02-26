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

    int ok;
    glGetProgramiv(r->program, GL_LINK_STATUS, &ok);
    if (!ok) {
        char log[512];
        glGetProgramInfoLog(r->program, sizeof(log), NULL, log);
        fprintf(stderr, "Program link error: %s\n", log);
        return -1;
    }

    glDeleteShader(vs);
    glDeleteShader(fs);

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

void renderer_upload_target_line(Renderer *r, float cx, float cy, float tx, float ty)
{
    float verts[] = { cx, cy, tx, ty };
    if (!r->line_vao) {
        glGenVertexArrays(1, &r->line_vao);
        glGenBuffers(1, &r->line_vbo);
    }
    glBindVertexArray(r->line_vao);
    glBindBuffer(GL_ARRAY_BUFFER, r->line_vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(verts), verts, GL_DYNAMIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, NULL);
    glBindVertexArray(0);
}

void renderer_upload_markers(Renderer *r, float cx, float cy, float tx, float ty, float size_km)
{
    float s = size_km;
    float verts[] = {
        /* Center crosshair */
        cx - s, cy,     cx + s, cy,
        cx,     cy - s, cx,     cy + s,
        /* Target crosshair */
        tx - s, ty,     tx + s, ty,
        tx,     ty - s, tx,     ty + s,
    };
    if (!r->marker_vao) {
        glGenVertexArrays(1, &r->marker_vao);
        glGenBuffers(1, &r->marker_vbo);
    }
    glBindVertexArray(r->marker_vao);
    glBindBuffer(GL_ARRAY_BUFFER, r->marker_vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(verts), verts, GL_DYNAMIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, NULL);
    glBindVertexArray(0);
}

void renderer_upload_earth_circle(Renderer *r)
{
    int n = 360;
    float *verts = malloc(n * 2 * sizeof(float));
    double radius = EARTH_MAX_PROJ_RADIUS;
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
    free(verts);
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
    glClear(GL_COLOR_BUFFER_BIT);
    glUseProgram(r->program);
    glUniformMatrix4fv(r->mvp_loc, 1, GL_FALSE, mvp);

    /* Earth boundary circle - dark blue */
    if (r->circle_vao) {
        glUniform3f(r->color_loc, 0.15f, 0.15f, 0.3f);
        glBindVertexArray(r->circle_vao);
        glDrawArrays(GL_LINE_LOOP, 0, r->circle_vertex_count);
    }

    /* Country borders - dim gray */
    if (r->border_vao) {
        glUniform3f(r->color_loc, 0.4f, 0.4f, 0.5f);
        glBindVertexArray(r->border_vao);
        for (int i = 0; i < r->border_num_segments; i++) {
            glDrawArrays(GL_LINE_STRIP,
                         r->border_segment_starts[i],
                         r->border_segment_counts[i]);
        }
    }

    /* Coastlines - green */
    if (r->map_vao) {
        glUniform3f(r->color_loc, 0.2f, 0.8f, 0.3f);
        glBindVertexArray(r->map_vao);
        for (int i = 0; i < r->map_num_segments; i++) {
            glDrawArrays(GL_LINE_STRIP,
                         r->map_segment_starts[i],
                         r->map_segment_counts[i]);
        }
    }

    /* Target line - yellow */
    if (r->line_vao) {
        glUniform3f(r->color_loc, 1.0f, 0.9f, 0.2f);
        glBindVertexArray(r->line_vao);
        glDrawArrays(GL_LINES, 0, 2);
    }

    /* Markers - white for center, red for target */
    if (r->marker_vao) {
        glBindVertexArray(r->marker_vao);
        glUniform3f(r->color_loc, 1.0f, 1.0f, 1.0f);
        glDrawArrays(GL_LINES, 0, 4);
        glUniform3f(r->color_loc, 1.0f, 0.3f, 0.2f);
        glDrawArrays(GL_LINES, 4, 4);
    }

    /* Text overlay — switch to pixel-space orthographic matrix */
    if (r->text_vao && r->text_vertex_count > 0 && fb_w > 0 && fb_h > 0) {
        float ortho[16];
        memset(ortho, 0, sizeof(ortho));
        ortho[0]  = 2.0f / (float)fb_w;
        ortho[5]  = -2.0f / (float)fb_h;  /* y down */
        ortho[10] = -1.0f;
        ortho[12] = -1.0f;
        ortho[13] = 1.0f;
        ortho[15] = 1.0f;
        glUniformMatrix4fv(r->mvp_loc, 1, GL_FALSE, ortho);

        glUniform3f(r->color_loc, 1.0f, 1.0f, 1.0f);
        glBindVertexArray(r->text_vao);
        glDrawArrays(GL_LINES, 0, r->text_vertex_count);
    }

    glBindVertexArray(0);
}

void renderer_destroy(Renderer *r)
{
    glDeleteProgram(r->program);
    if (r->map_vao) { glDeleteVertexArrays(1, &r->map_vao); glDeleteBuffers(1, &r->map_vbo); }
    if (r->border_vao) { glDeleteVertexArrays(1, &r->border_vao); glDeleteBuffers(1, &r->border_vbo); }
    if (r->line_vao) { glDeleteVertexArrays(1, &r->line_vao); glDeleteBuffers(1, &r->line_vbo); }
    if (r->marker_vao) { glDeleteVertexArrays(1, &r->marker_vao); glDeleteBuffers(1, &r->marker_vbo); }
    if (r->circle_vao) { glDeleteVertexArrays(1, &r->circle_vao); glDeleteBuffers(1, &r->circle_vbo); }
    if (r->text_vao) { glDeleteVertexArrays(1, &r->text_vao); glDeleteBuffers(1, &r->text_vbo); }
}
