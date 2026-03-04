#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "overlay.h"
#include "projection.h"
#include "cJSON.h"

/* ── MUF contour lines ─────────────────────────────────────────── */

void muf_data_init(MufData *m)
{
    memset(m, 0, sizeof(*m));
}

void muf_data_free(MufData *m)
{
    free(m->raw_lats);
    free(m->raw_lons);
    free(m->vertices);
    memset(m, 0, sizeof(*m));
}

/* Parse hex color string "#RRGGBB" → RGBA float (alpha=1) */
static void hex_to_rgba(const char *hex, float rgba[4])
{
    unsigned int r = 0, g = 0, b = 0;
    if (hex && hex[0] == '#' && strlen(hex) >= 7)
        sscanf(hex + 1, "%02x%02x%02x", &r, &g, &b);
    rgba[0] = r / 255.0f;
    rgba[1] = g / 255.0f;
    rgba[2] = b / 255.0f;
    rgba[3] = 1.0f;
}

int muf_parse_geojson(const char *json_str, MufData *m)
{
    cJSON *root = cJSON_Parse(json_str);
    if (!root) return -1;

    cJSON *features = cJSON_GetObjectItem(root, "features");
    if (!cJSON_IsArray(features)) { cJSON_Delete(root); return -1; }

    /* First pass: count total coordinates across all LineString features */
    int total_coords = 0;
    int total_segs = 0;
    cJSON *feat;
    cJSON_ArrayForEach(feat, features) {
        cJSON *geom = cJSON_GetObjectItem(feat, "geometry");
        if (!geom) continue;
        cJSON *type = cJSON_GetObjectItem(geom, "type");
        if (!type || !cJSON_IsString(type)) continue;
        if (strcmp(type->valuestring, "LineString") != 0) continue;
        cJSON *coords = cJSON_GetObjectItem(geom, "coordinates");
        if (!cJSON_IsArray(coords)) continue;
        int n = cJSON_GetArraySize(coords);
        if (n < 2) continue;
        total_coords += n;
        total_segs++;
    }

    if (total_coords == 0 || total_segs == 0) {
        cJSON_Delete(root);
        return -1;
    }

    /* Allocate raw storage */
    free(m->raw_lats);
    free(m->raw_lons);
    m->raw_lats = malloc(total_coords * sizeof(double));
    m->raw_lons = malloc(total_coords * sizeof(double));
    if (!m->raw_lats || !m->raw_lons) {
        cJSON_Delete(root);
        return -1;
    }

    /* Second pass: extract coordinates, colors, and legend entries */
    m->raw_count = 0;
    m->raw_num_segments = 0;
    m->legend_count = 0;

    cJSON_ArrayForEach(feat, features) {
        if (m->raw_num_segments >= MUF_MAX_SEGMENTS) break;

        cJSON *geom = cJSON_GetObjectItem(feat, "geometry");
        if (!geom) continue;
        cJSON *type = cJSON_GetObjectItem(geom, "type");
        if (!type || !cJSON_IsString(type) ||
            strcmp(type->valuestring, "LineString") != 0) continue;
        cJSON *coords = cJSON_GetObjectItem(geom, "coordinates");
        if (!cJSON_IsArray(coords)) continue;
        int n = cJSON_GetArraySize(coords);
        if (n < 2) continue;

        /* Get stroke color and level-value from properties */
        float color[4] = { 1.0f, 1.0f, 0.0f, 1.0f }; /* default yellow */
        float mhz = 0.0f;
        cJSON *props = cJSON_GetObjectItem(feat, "properties");
        if (props) {
            cJSON *stroke = cJSON_GetObjectItem(props, "stroke");
            if (stroke && cJSON_IsString(stroke))
                hex_to_rgba(stroke->valuestring, color);
            cJSON *level = cJSON_GetObjectItem(props, "level-value");
            if (level && cJSON_IsNumber(level))
                mhz = (float)level->valuedouble;
        }

        /* Add unique legend entry (deduplicate by mhz value) */
        if (mhz > 0.0f && m->legend_count < MUF_MAX_LEGEND) {
            int found = 0;
            for (int li = 0; li < m->legend_count; li++) {
                if (m->legend[li].mhz == mhz) { found = 1; break; }
            }
            if (!found) {
                m->legend[m->legend_count].mhz = mhz;
                memcpy(m->legend[m->legend_count].color, color, sizeof(color));
                m->legend_count++;
            }
        }

        int seg_idx = m->raw_num_segments;
        m->raw_seg_starts[seg_idx] = m->raw_count;
        memcpy(m->raw_seg_colors[seg_idx], color, sizeof(color));

        cJSON *coord;
        int count = 0;
        cJSON_ArrayForEach(coord, coords) {
            if (!cJSON_IsArray(coord) || cJSON_GetArraySize(coord) < 2) continue;
            cJSON *lon_j = cJSON_GetArrayItem(coord, 0);
            cJSON *lat_j = cJSON_GetArrayItem(coord, 1);
            if (!cJSON_IsNumber(lon_j) || !cJSON_IsNumber(lat_j)) continue;
            m->raw_lons[m->raw_count] = lon_j->valuedouble;
            m->raw_lats[m->raw_count] = lat_j->valuedouble;
            m->raw_count++;
            count++;
        }

        m->raw_seg_counts[seg_idx] = count;
        m->raw_num_segments++;
    }

    cJSON_Delete(root);

    /* Sort legend entries by MHz ascending */
    for (int i = 0; i < m->legend_count - 1; i++) {
        for (int j = i + 1; j < m->legend_count; j++) {
            if (m->legend[j].mhz < m->legend[i].mhz) {
                MufLegendEntry tmp = m->legend[i];
                m->legend[i] = m->legend[j];
                m->legend[j] = tmp;
            }
        }
    }

    /* Project into km-space */
    muf_reproject(m);
    return 0;
}

#define MUF_SPLIT_THRESHOLD_KM 5000.0f

void muf_reproject(MufData *m)
{
    if (m->raw_count == 0) return;

    /* Project all raw vertices */
    float *proj = malloc(m->raw_count * 2 * sizeof(float));
    if (!proj) return;
    for (int i = 0; i < m->raw_count; i++) {
        double x, y;
        projection_forward(m->raw_lats[i], m->raw_lons[i], &x, &y);
        proj[i * 2]     = (float)x;
        proj[i * 2 + 1] = (float)y;
    }

    free(m->vertices);
    m->vertices = proj;
    m->vertex_count = m->raw_count;
    m->num_segments = 0;

    /* Split segments at large jumps (same as map_data.c) */
    for (int s = 0; s < m->raw_num_segments && m->num_segments < MUF_MAX_SEGMENTS; s++) {
        int base = m->raw_seg_starts[s];
        int count = m->raw_seg_counts[s];
        int seg_start = base;

        for (int v = 1; v < count; v++) {
            int idx = base + v;
            int prev = idx - 1;
            float dx = proj[idx * 2]     - proj[prev * 2];
            float dy = proj[idx * 2 + 1] - proj[prev * 2 + 1];
            float dist = dx * dx + dy * dy;

            if (dist > MUF_SPLIT_THRESHOLD_KM * MUF_SPLIT_THRESHOLD_KM) {
                int sub_count = (base + v) - seg_start;
                if (sub_count >= 2 && m->num_segments < MUF_MAX_SEGMENTS) {
                    int si = m->num_segments;
                    m->segment_starts[si] = seg_start;
                    m->segment_counts[si] = sub_count;
                    memcpy(m->segment_colors[si], m->raw_seg_colors[s], 4 * sizeof(float));
                    m->num_segments++;
                }
                seg_start = base + v;
            }
        }

        /* Flush remaining */
        int sub_count = (base + count) - seg_start;
        if (sub_count >= 2 && m->num_segments < MUF_MAX_SEGMENTS) {
            int si = m->num_segments;
            m->segment_starts[si] = seg_start;
            m->segment_counts[si] = sub_count;
            memcpy(m->segment_colors[si], m->raw_seg_colors[s], 4 * sizeof(float));
            m->num_segments++;
        }
    }
}

/* ── Aurora heatmap ────────────────────────────────────────────── */

void aurora_grid_init(AuroraGrid *g)
{
    g->values = NULL;
    g->valid = 0;
}

void aurora_grid_free(AuroraGrid *g)
{
    free(g->values);
    g->values = NULL;
    g->valid = 0;
}

int aurora_parse_json(const char *json_str, AuroraGrid *g)
{
    cJSON *root = cJSON_Parse(json_str);
    if (!root) return -1;

    cJSON *coords = cJSON_GetObjectItem(root, "coordinates");
    if (!cJSON_IsArray(coords)) { cJSON_Delete(root); return -1; }

    /* Allocate grid: 360 lons × 181 lats (0-359 × -90 to 90) */
    int grid_size = 360 * 181;
    if (!g->values) {
        g->values = calloc(grid_size, sizeof(int));
        if (!g->values) { cJSON_Delete(root); return -1; }
    } else {
        memset(g->values, 0, grid_size * sizeof(int));
    }

    cJSON *triplet;
    cJSON_ArrayForEach(triplet, coords) {
        if (!cJSON_IsArray(triplet) || cJSON_GetArraySize(triplet) < 3) continue;
        int lon = (int)cJSON_GetArrayItem(triplet, 0)->valuedouble;
        int lat = (int)cJSON_GetArrayItem(triplet, 1)->valuedouble;
        int val = (int)cJSON_GetArrayItem(triplet, 2)->valuedouble;

        /* Normalize lon to 0-359 */
        if (lon < 0) lon += 360;
        if (lon >= 360) lon -= 360;
        int lat_idx = lat + 90;
        if (lat_idx < 0 || lat_idx > 180 || lon < 0 || lon >= 360) continue;
        g->values[lon * 181 + lat_idx] = val;
    }

    g->valid = 1;
    cJSON_Delete(root);
    return 0;
}

void aurora_mesh_init(AuroraMesh *m)
{
    /* Same grid resolution as nightmesh */
    #define AURORA_ANGULAR_DIVS  180
    #define AURORA_RADIAL_DIVS    60
    int max_verts = AURORA_ANGULAR_DIVS * 3 +
                    AURORA_ANGULAR_DIVS * (AURORA_RADIAL_DIVS - 1) * 6;
    m->vertices = malloc(max_verts * 3 * sizeof(float));
    if (!m->vertices) { m->capacity = 0; m->vertex_count = 0; return; }
    m->capacity = max_verts;
    m->vertex_count = 0;
}

void aurora_mesh_free(AuroraMesh *m)
{
    free(m->vertices);
    m->vertices = NULL;
    m->vertex_count = 0;
    m->capacity = 0;
}

static void aurora_emit(AuroraMesh *m, float x, float y, float alpha)
{
    if (m->vertex_count < m->capacity) {
        int i = m->vertex_count * 3;
        m->vertices[i]     = x;
        m->vertices[i + 1] = y;
        m->vertices[i + 2] = alpha;
        m->vertex_count++;
    }
}

/* Look up aurora probability at lat/lon in the grid, return alpha 0-1 */
static float aurora_lookup(const AuroraGrid *g, double lat, double lon)
{
    /* Normalize longitude to 0-359 */
    if (lon < 0.0) lon += 360.0;
    if (lon >= 360.0) lon -= 360.0;

    int ilon = (int)(lon + 0.5);
    if (ilon >= 360) ilon = 0;
    int ilat = (int)(lat + 90.0 + 0.5);
    if (ilat < 0) ilat = 0;
    if (ilat > 180) ilat = 180;

    int val = g->values[ilon * 181 + ilat];

    /* Map probability to alpha:
     * 0-5   → transparent (skip noise)
     * 5-50  → ramp from 0.0 to 0.5
     * 50-100 → ramp from 0.5 to 0.75 */
    if (val <= 5) return 0.0f;
    if (val <= 50) return (float)(val - 5) / 45.0f * 0.5f;
    return 0.5f + (float)(val - 50) / 50.0f * 0.25f;
}

void aurora_mesh_build(AuroraMesh *m, const AuroraGrid *g)
{
    if (!g || !g->valid || !m->vertices) return;

    double max_r = projection_get_radius() - 0.5;
    float dr = (float)(max_r / AURORA_RADIAL_DIVS);
    float da = 2.0f * (float)M_PI / AURORA_ANGULAR_DIVS;

    m->vertex_count = 0;

    /* Precompute alpha grid */
    int rows = AURORA_RADIAL_DIVS + 1;
    int cols = AURORA_ANGULAR_DIVS;
    float *alpha_grid = malloc(rows * cols * sizeof(float));
    if (!alpha_grid) return;

    for (int ri = 0; ri <= AURORA_RADIAL_DIVS; ri++) {
        float r = ri * dr;
        for (int ai = 0; ai < AURORA_ANGULAR_DIVS; ai++) {
            float a = ai * da;
            float x = r * cosf(a);
            float y = r * sinf(a);
            float av = 0.0f;

            double lat, lon;
            if (ri == 0) {
                if (projection_inverse(0.0, 0.0, &lat, &lon) == 0)
                    av = aurora_lookup(g, lat, lon);
            } else {
                if (projection_inverse((double)x, (double)y, &lat, &lon) == 0)
                    av = aurora_lookup(g, lat, lon);
                /* Outside globe: no aurora */
            }
            alpha_grid[ri * cols + ai] = av;
        }
    }

    /* Generate triangles — same pattern as nightmesh */
    float center_alpha = alpha_grid[0];

    for (int ai = 0; ai < AURORA_ANGULAR_DIVS; ai++) {
        int ai_next = (ai + 1) % AURORA_ANGULAR_DIVS;
        float a0 = ai * da;
        float a1 = (ai + 1) * da;

        /* Inner ring */
        {
            float a_v0 = center_alpha;
            float a_v1 = alpha_grid[1 * cols + ai];
            float a_v2 = alpha_grid[1 * cols + ai_next];
            if (a_v0 > 0.0f || a_v1 > 0.0f || a_v2 > 0.0f) {
                float r1 = dr;
                aurora_emit(m, 0.0f, 0.0f, a_v0);
                aurora_emit(m, r1 * cosf(a0), r1 * sinf(a0), a_v1);
                aurora_emit(m, r1 * cosf(a1), r1 * sinf(a1), a_v2);
            }
        }

        /* Outer rings */
        for (int ri = 1; ri < AURORA_RADIAL_DIVS; ri++) {
            float r0 = ri * dr;
            float r1 = (ri + 1) * dr;

            float cos_a0 = cosf(a0), sin_a0 = sinf(a0);
            float cos_a1 = cosf(a1), sin_a1 = sinf(a1);

            float a00 = alpha_grid[ri * cols + ai];
            float a01 = alpha_grid[ri * cols + ai_next];
            float a10 = alpha_grid[(ri + 1) * cols + ai];
            float a11 = alpha_grid[(ri + 1) * cols + ai_next];

            if (a00 == 0.0f && a01 == 0.0f && a10 == 0.0f && a11 == 0.0f)
                continue;

            aurora_emit(m, r0 * cos_a0, r0 * sin_a0, a00);
            aurora_emit(m, r1 * cos_a0, r1 * sin_a0, a10);
            aurora_emit(m, r1 * cos_a1, r1 * sin_a1, a11);

            aurora_emit(m, r0 * cos_a0, r0 * sin_a0, a00);
            aurora_emit(m, r1 * cos_a1, r1 * sin_a1, a11);
            aurora_emit(m, r0 * cos_a1, r0 * sin_a1, a01);
        }
    }

    free(alpha_grid);
}
