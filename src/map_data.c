#include <stdio.h>
#include <stdlib.h>
#include <shapefil.h>
#include "map_data.h"
#include "projection.h"

static int load_raw(MapData *md, const char *shp_path)
{
    SHPHandle shp = SHPOpen(shp_path, "rb");
    if (!shp) {
        fprintf(stderr, "Error: cannot open shapefile: %s\n", shp_path);
        return -1;
    }

    int num_entities, shape_type;
    SHPGetInfo(shp, &num_entities, &shape_type, NULL, NULL);

    /* First pass: count total vertices */
    int total = 0;
    int seg_count = 0;
    for (int i = 0; i < num_entities; i++) {
        SHPObject *obj = SHPReadObject(shp, i);
        if (!obj) continue;
        for (int p = 0; p < obj->nParts; p++) {
            int start = obj->panPartStart[p];
            int end = (p + 1 < obj->nParts) ? obj->panPartStart[p + 1] : obj->nVertices;
            int count = end - start;
            if (count > 1) {
                total += count;
                seg_count++;
            }
        }
        SHPDestroyObject(obj);
    }

    if (seg_count > MAX_SEGMENTS) {
        fprintf(stderr, "Warning: truncating to %d segments\n", MAX_SEGMENTS);
        seg_count = MAX_SEGMENTS;
    }

    /* Allocate raw storage */
    free(md->raw_lats);
    free(md->raw_lons);
    md->raw_lats = malloc(total * sizeof(double));
    md->raw_lons = malloc(total * sizeof(double));
    if (!md->raw_lats || !md->raw_lons) {
        SHPClose(shp);
        return -1;
    }

    /* Second pass: read vertices */
    md->raw_count = 0;
    md->raw_num_segments = 0;
    for (int i = 0; i < num_entities && md->raw_num_segments < MAX_SEGMENTS; i++) {
        SHPObject *obj = SHPReadObject(shp, i);
        if (!obj) continue;
        for (int p = 0; p < obj->nParts && md->raw_num_segments < MAX_SEGMENTS; p++) {
            int start = obj->panPartStart[p];
            int end = (p + 1 < obj->nParts) ? obj->panPartStart[p + 1] : obj->nVertices;
            int count = end - start;
            if (count <= 1) continue;

            md->raw_seg_starts[md->raw_num_segments] = md->raw_count;
            md->raw_seg_counts[md->raw_num_segments] = count;
            md->raw_num_segments++;

            for (int v = start; v < end; v++) {
                md->raw_lons[md->raw_count] = obj->padfX[v];
                md->raw_lats[md->raw_count] = obj->padfY[v];
                md->raw_count++;
            }
        }
        SHPDestroyObject(obj);
    }

    SHPClose(shp);
    return 0;
}

/* Max distance (km) between consecutive projected vertices before splitting.
 * Segments crossing near the antipodal point produce huge jumps. */
#define SPLIT_THRESHOLD_KM 5000.0f

static void project_all(MapData *md)
{
    /* First pass: project all raw vertices */
    float *proj = malloc(md->raw_count * 2 * sizeof(float));
    for (int i = 0; i < md->raw_count; i++) {
        double x, y;
        projection_forward(md->raw_lats[i], md->raw_lons[i], &x, &y);
        proj[i * 2]     = (float)x;
        proj[i * 2 + 1] = (float)y;
    }

    /* Second pass: split segments where consecutive points jump too far.
     * Output may have more segments than input (but same vertex count). */
    free(md->vertices);
    md->vertices = proj;
    md->vertex_count = md->raw_count;
    md->num_segments = 0;

    for (int s = 0; s < md->raw_num_segments && md->num_segments < MAX_SEGMENTS; s++) {
        int base = md->raw_seg_starts[s];
        int count = md->raw_seg_counts[s];
        int seg_start = base;

        for (int v = 1; v < count; v++) {
            int idx = base + v;
            int prev = idx - 1;
            float dx = proj[idx * 2]     - proj[prev * 2];
            float dy = proj[idx * 2 + 1] - proj[prev * 2 + 1];
            float dist = dx * dx + dy * dy;

            if (dist > SPLIT_THRESHOLD_KM * SPLIT_THRESHOLD_KM) {
                /* End current sub-segment before the jump */
                int sub_count = (base + v) - seg_start;
                if (sub_count >= 2 && md->num_segments < MAX_SEGMENTS) {
                    md->segment_starts[md->num_segments] = seg_start;
                    md->segment_counts[md->num_segments] = sub_count;
                    md->num_segments++;
                }
                seg_start = base + v; /* Start new sub-segment after the jump */
            }
        }

        /* Flush remaining sub-segment */
        int sub_count = (base + count) - seg_start;
        if (sub_count >= 2 && md->num_segments < MAX_SEGMENTS) {
            md->segment_starts[md->num_segments] = seg_start;
            md->segment_counts[md->num_segments] = sub_count;
            md->num_segments++;
        }
    }
}

int map_data_load(MapData *md, const char *shp_path)
{
    md->vertices = NULL;
    md->vertex_count = 0;
    md->num_segments = 0;
    md->raw_lats = NULL;
    md->raw_lons = NULL;
    md->raw_count = 0;
    md->raw_num_segments = 0;

    if (load_raw(md, shp_path) != 0) return -1;
    project_all(md);
    return 0;
}

void map_data_reproject(MapData *md, const char *shp_path)
{
    (void)shp_path;
    if (md->raw_count > 0) {
        project_all(md);
    }
}

void map_data_free(MapData *md)
{
    free(md->vertices);
    md->vertices = NULL;
    md->vertex_count = 0;
    md->num_segments = 0;
    free(md->raw_lats);
    free(md->raw_lons);
    md->raw_lats = NULL;
    md->raw_lons = NULL;
    md->raw_count = 0;
    md->raw_num_segments = 0;
}
