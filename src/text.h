#ifndef TEXT_H
#define TEXT_H

/* Initialize the stroke font lookup table. Call once at startup. */
void text_init(void);

/* Build line-segment vertices for a string.
 * x, y: top-left position in pixels (y increases downward).
 * size: character height in pixels.
 * out_verts: output buffer of x,y pairs (2 floats per vertex, 2 vertices per segment).
 * max_verts: capacity of out_verts in vertices (not floats).
 * Returns number of vertices written (always even, for GL_LINES). */
int text_build(const char *str, float x, float y, float size,
               float *out_verts, int max_verts);

/* Compute the rendered width of a string in pixels. */
float text_width(const char *str, float size);

#endif
