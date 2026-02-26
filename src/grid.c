#include <math.h>
#include <stdlib.h>
#include "grid.h"
#include "projection.h"

#define RING_STEP_KM   5000.0   /* distance between concentric range rings */
#define AZIMUTH_STEP    30.0    /* degrees between radial lines */
#define CIRCLE_PTS        72    /* points per ring */

void grid_build(MapData *md)
{
    md->vertex_count = 0;
    md->num_segments = 0;

    double max_r = EARTH_MAX_PROJ_RADIUS;   /* ~20015 km */
    int num_rings = (int)(max_r / RING_STEP_KM);
    int num_radials = (int)(360.0 / AZIMUTH_STEP);
    int max_verts = num_rings * (CIRCLE_PTS + 1) + num_radials * 2;

    free(md->vertices);
    md->vertices = malloc(max_verts * 2 * sizeof(float));
    if (!md->vertices) return;

    /* Concentric range rings */
    for (int ri = 1; ri <= num_rings; ri++) {
        float r = (float)(ri * RING_STEP_KM);
        int start = md->vertex_count;
        for (int i = 0; i <= CIRCLE_PTS; i++) {
            float a = 2.0f * (float)M_PI * i / CIRCLE_PTS;
            md->vertices[md->vertex_count * 2]     = r * cosf(a);
            md->vertices[md->vertex_count * 2 + 1] = r * sinf(a);
            md->vertex_count++;
        }
        if (md->num_segments < MAX_SEGMENTS) {
            md->segment_starts[md->num_segments] = start;
            md->segment_counts[md->num_segments] = md->vertex_count - start;
            md->num_segments++;
        }
    }

    /* Radial azimuth lines from center to edge */
    for (int i = 0; i < num_radials; i++) {
        float a = 2.0f * (float)M_PI * i / num_radials;
        int start = md->vertex_count;
        md->vertices[md->vertex_count * 2]     = 0.0f;
        md->vertices[md->vertex_count * 2 + 1] = 0.0f;
        md->vertex_count++;
        md->vertices[md->vertex_count * 2]     = (float)max_r * cosf(a);
        md->vertices[md->vertex_count * 2 + 1] = (float)max_r * sinf(a);
        md->vertex_count++;
        if (md->num_segments < MAX_SEGMENTS) {
            md->segment_starts[md->num_segments] = start;
            md->segment_counts[md->num_segments] = 2;
            md->num_segments++;
        }
    }
}
