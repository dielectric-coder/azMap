#include <math.h>
#include <stdlib.h>
#include "grid.h"
#include "projection.h"

#define RING_STEP_KM   5000.0   /* distance between concentric range rings */
#define AZIMUTH_STEP    30.0    /* degrees between radial lines */
#define CIRCLE_PTS        72    /* points per ring */

/* Geographic graticule settings */
#define GEO_LAT_STEP   30.0    /* degrees between parallels */
#define GEO_LON_STEP   30.0    /* degrees between meridians */
#define GEO_SAMPLE_STEP 5.0    /* degrees between sample points */

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

void grid_build_geo(MapData *md)
{
    md->vertex_count = 0;
    md->num_segments = 0;

    /* Parallels: -60 to 60 every 30 deg, meridians: every 30 deg */
    int num_parallels = (int)(120.0 / GEO_LAT_STEP) + 1; /* -60 to 60 */
    int pts_per_parallel = (int)(360.0 / GEO_SAMPLE_STEP) + 1; /* 73 */
    int num_meridians = (int)(360.0 / GEO_LON_STEP);      /* 12 */
    int pts_per_meridian = (int)(180.0 / GEO_SAMPLE_STEP) + 1; /* 37 */
    int max_verts = num_parallels * pts_per_parallel + num_meridians * pts_per_meridian;

    free(md->vertices);
    md->vertices = malloc(max_verts * 2 * sizeof(float));
    if (!md->vertices) return;

    /* Parallels */
    for (double lat = -60.0; lat <= 60.0 + 0.01; lat += GEO_LAT_STEP) {
        int seg_start = md->vertex_count;
        int in_seg = 0;
        for (double lon = -180.0; lon <= 180.0 + 0.01; lon += GEO_SAMPLE_STEP) {
            double x, y;
            int rc = projection_forward(lat, lon, &x, &y);
            if (rc < 0) {
                /* Back hemisphere — flush current segment */
                if (in_seg >= 2 && md->num_segments < MAX_SEGMENTS) {
                    md->segment_starts[md->num_segments] = seg_start;
                    md->segment_counts[md->num_segments] = in_seg;
                    md->num_segments++;
                }
                /* Discard isolated point */
                if (in_seg == 1) md->vertex_count--;
                in_seg = 0;
                seg_start = md->vertex_count;
                continue;
            }
            md->vertices[md->vertex_count * 2]     = (float)x;
            md->vertices[md->vertex_count * 2 + 1] = (float)y;
            md->vertex_count++;
            in_seg++;
        }
        if (in_seg >= 2 && md->num_segments < MAX_SEGMENTS) {
            md->segment_starts[md->num_segments] = seg_start;
            md->segment_counts[md->num_segments] = in_seg;
            md->num_segments++;
        }
        if (in_seg == 1) md->vertex_count--;
    }

    /* Meridians */
    for (double lon = -180.0; lon < 180.0 - 0.01; lon += GEO_LON_STEP) {
        int seg_start = md->vertex_count;
        int in_seg = 0;
        for (double lat = -90.0; lat <= 90.0 + 0.01; lat += GEO_SAMPLE_STEP) {
            double x, y;
            int rc = projection_forward(lat, lon, &x, &y);
            if (rc < 0) {
                if (in_seg >= 2 && md->num_segments < MAX_SEGMENTS) {
                    md->segment_starts[md->num_segments] = seg_start;
                    md->segment_counts[md->num_segments] = in_seg;
                    md->num_segments++;
                }
                if (in_seg == 1) md->vertex_count--;
                in_seg = 0;
                seg_start = md->vertex_count;
                continue;
            }
            md->vertices[md->vertex_count * 2]     = (float)x;
            md->vertices[md->vertex_count * 2 + 1] = (float)y;
            md->vertex_count++;
            in_seg++;
        }
        if (in_seg >= 2 && md->num_segments < MAX_SEGMENTS) {
            md->segment_starts[md->num_segments] = seg_start;
            md->segment_counts[md->num_segments] = in_seg;
            md->num_segments++;
        }
        if (in_seg == 1) md->vertex_count--;
    }
}
