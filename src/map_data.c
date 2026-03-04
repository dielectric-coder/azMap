#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
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
    if (!proj) return;
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

/* Find the lat/lon where a line segment crosses the hemisphere boundary.
 * Uses bisection: a_back indicates which endpoint is back-hemisphere. */
static void find_boundary_crossing(double lat_a, double lon_a, int a_back,
                                   double lat_b, double lon_b,
                                   double *out_lat, double *out_lon)
{
    double t_lo = 0.0, t_hi = 1.0;
    for (int i = 0; i < 12; i++) {
        double t = (t_lo + t_hi) * 0.5;
        double lat = lat_a + t * (lat_b - lat_a);
        double lon = lon_a + t * (lon_b - lon_a);
        double x, y;
        int mid_back = (projection_forward(lat, lon, &x, &y) != 0);
        if (mid_back == a_back)
            t_lo = t;
        else
            t_hi = t;
    }
    double t = (t_lo + t_hi) * 0.5;
    *out_lat = lat_a + t * (lat_b - lat_a);
    *out_lon = lon_a + t * (lon_b - lon_a);
}

static void project_nosplit(MapData *md)
{
    free(md->vertices);

    /* Check each raw vertex for back-hemisphere */
    int *back = malloc(md->raw_count * sizeof(int));
    if (!back) { md->vertices = NULL; md->vertex_count = 0; md->num_segments = 0; return; }
    for (int i = 0; i < md->raw_count; i++) {
        double x, y;
        back[i] = (projection_forward(md->raw_lats[i], md->raw_lons[i], &x, &y) != 0);
    }

    /* In AZEQ mode, mark vertices beyond 160° angular distance as "far".
     * Rings containing any far vertex are skipped entirely (not clipped)
     * to avoid stencil triangle-fan artifacts from distorted geometry. */
    int is_azeq = (projection_get_mode() == PROJ_AZEQ);
    int *far = NULL;
    if (is_azeq) {
        double clat, clon;
        projection_get_center(&clat, &clon);
        double max_dist = 160.0 / 180.0 * M_PI * EARTH_RADIUS_KM;
        far = malloc(md->raw_count * sizeof(int));
        if (far) {
            for (int i = 0; i < md->raw_count; i++)
                far[i] = (projection_distance(clat, clon,
                            md->raw_lats[i], md->raw_lons[i]) > max_dist);
        }
    }

    /* Build clipped rings — each edge can add one intersection vertex */
    int max_out = md->raw_count * 2;
    double *clip_lats = malloc(max_out * sizeof(double));
    double *clip_lons = malloc(max_out * sizeof(double));
    if (!clip_lats || !clip_lons) {
        free(clip_lats); free(clip_lons); free(back); free(far);
        md->vertices = NULL; md->vertex_count = 0; md->num_segments = 0;
        return;
    }
    int clip_count = 0;

    md->num_segments = md->raw_num_segments;

    for (int s = 0; s < md->raw_num_segments; s++) {
        int base = md->raw_seg_starts[s];
        int count = md->raw_seg_counts[s];
        int ring_start = clip_count;

        /* In AZEQ mode, skip entire ring if any vertex is near antipodal */
        if (far) {
            int has_far = 0;
            for (int v = 0; v < count; v++) {
                if (far[base + v]) { has_far = 1; break; }
            }
            if (has_far) {
                md->segment_starts[s] = ring_start;
                md->segment_counts[s] = 0;
                md->segment_clamped[s] = 1;
                continue;
            }
        }

        /* Check for back-hemisphere vertices */
        int has_back = 0, has_front = 0;
        for (int v = 0; v < count; v++) {
            if (back[base + v]) has_back = 1; else has_front = 1;
        }

        if (!has_front) {
            /* Entirely back-hemisphere — skip */
            md->segment_starts[s] = ring_start;
            md->segment_counts[s] = 0;
            md->segment_clamped[s] = 1;
            continue;
        }

        md->segment_clamped[s] = 0;

        if (!has_back) {
            /* Entirely front-hemisphere — copy as-is */
            for (int v = 0; v < count; v++) {
                clip_lats[clip_count] = md->raw_lats[base + v];
                clip_lons[clip_count] = md->raw_lons[base + v];
                clip_count++;
            }
        } else {
            /* Clip: emit front vertices and boundary crossings, skip back vertices */
            for (int v = 0; v < count; v++) {
                int ci = base + v;
                int ni = base + (v + 1) % count;

                if (!back[ci]) {
                    clip_lats[clip_count] = md->raw_lats[ci];
                    clip_lons[clip_count] = md->raw_lons[ci];
                    clip_count++;
                }

                if (back[ci] != back[ni]) {
                    double blat, blon;
                    find_boundary_crossing(
                        md->raw_lats[ci], md->raw_lons[ci], back[ci],
                        md->raw_lats[ni], md->raw_lons[ni],
                        &blat, &blon);
                    clip_lats[clip_count] = blat;
                    clip_lons[clip_count] = blon;
                    clip_count++;
                }
            }
        }

        int seg_count = clip_count - ring_start;
        if (seg_count < 3) {
            /* Too few vertices for a triangle fan — discard */
            clip_count = ring_start;
            md->segment_starts[s] = ring_start;
            md->segment_counts[s] = 0;
            md->segment_clamped[s] = 1;
        } else {
            md->segment_starts[s] = ring_start;
            md->segment_counts[s] = seg_count;
        }
    }

    /* Project clipped vertices */
    md->vertices = malloc(clip_count * 2 * sizeof(float));
    if (!md->vertices) {
        free(clip_lats); free(clip_lons); free(back); free(far);
        md->vertex_count = 0; md->num_segments = 0;
        return;
    }
    for (int i = 0; i < clip_count; i++) {
        double x, y;
        projection_forward_clamped(clip_lats[i], clip_lons[i], &x, &y);
        md->vertices[i * 2]     = (float)x;
        md->vertices[i * 2 + 1] = (float)y;
    }
    md->vertex_count = clip_count;

    /* Ortho mode: insert boundary arc vertices for "shortcut" edges.
     * When clipping cuts a polygon ring at the hemisphere boundary, it
     * creates direct edges between crossing points that don't follow the
     * boundary circle arc. These shortcut edges cause the GL_TRIANGLE_FAN
     * stencil inversion to cover the wrong area.  Fix by detecting edges
     * where both endpoints are near the boundary circle and the edge is
     * long, then inserting intermediate points along the boundary arc. */
    if (projection_get_mode() == PROJ_ORTHO) {
        float Rf = (float)projection_get_radius();
        float min_r = Rf * 0.85f;          /* endpoints must be near boundary */
        float max_edge_sq = Rf * Rf * 0.25f; /* edge > 0.5*R triggers arc */
        #define ARC_PTS 12

        /* Count extra vertices needed */
        int extra = 0;
        for (int s = 0; s < md->num_segments; s++) {
            if (md->segment_clamped[s] || md->segment_counts[s] < 3) continue;
            int st = md->segment_starts[s], cnt = md->segment_counts[s];
            for (int v = 0; v < cnt; v++) {
                int ci = st + v, ni = st + (v + 1) % cnt;
                float x0 = md->vertices[ci*2], y0 = md->vertices[ci*2+1];
                float x1 = md->vertices[ni*2], y1 = md->vertices[ni*2+1];
                float r0 = sqrtf(x0*x0 + y0*y0);
                float r1 = sqrtf(x1*x1 + y1*y1);
                float dx = x1-x0, dy = y1-y0;
                if (r0 > min_r && r1 > min_r && dx*dx+dy*dy > max_edge_sq)
                    extra += ARC_PTS;
            }
        }

        if (extra > 0) {
            float *nv = malloc((md->vertex_count + extra) * 2 * sizeof(float));
            if (nv) {
                int nc = 0;
                for (int s = 0; s < md->num_segments; s++) {
                    int st = md->segment_starts[s], cnt = md->segment_counts[s];
                    int ns = nc;
                    if (md->segment_clamped[s] || cnt < 3) {
                        memcpy(nv + nc*2, md->vertices + st*2,
                               cnt * 2 * sizeof(float));
                        nc += cnt;
                    } else {
                        for (int v = 0; v < cnt; v++) {
                            int ci = st + v, ni = st + (v + 1) % cnt;
                            nv[nc*2]   = md->vertices[ci*2];
                            nv[nc*2+1] = md->vertices[ci*2+1];
                            nc++;

                            float x0 = md->vertices[ci*2], y0 = md->vertices[ci*2+1];
                            float x1 = md->vertices[ni*2], y1 = md->vertices[ni*2+1];
                            float r0 = sqrtf(x0*x0 + y0*y0);
                            float r1 = sqrtf(x1*x1 + y1*y1);
                            float dx = x1-x0, dy = y1-y0;
                            if (r0 > min_r && r1 > min_r && dx*dx+dy*dy > max_edge_sq) {
                                /* Insert arc along boundary circle */
                                float a0 = atan2f(y0, x0);
                                float a1 = atan2f(y1, x1);
                                float da = a1 - a0;
                                if (da >  (float)M_PI) da -= 2.0f*(float)M_PI;
                                if (da < -(float)M_PI) da += 2.0f*(float)M_PI;
                                for (int k = 1; k <= ARC_PTS; k++) {
                                    float t = a0 + da * (float)k / (float)(ARC_PTS + 1);
                                    nv[nc*2]   = Rf * cosf(t);
                                    nv[nc*2+1] = Rf * sinf(t);
                                    nc++;
                                }
                            }
                        }
                    }
                    md->segment_starts[s] = ns;
                    md->segment_counts[s] = nc - ns;
                }
                free(md->vertices);
                md->vertices = nv;
                md->vertex_count = nc;
            }
        }
    }

    free(clip_lats);
    free(clip_lons);
    free(back);
    free(far);
}

void map_data_reproject_nosplit(MapData *md)
{
    if (md->raw_count > 0)
        project_nosplit(md);
}

void map_data_reproject(MapData *md)
{
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
