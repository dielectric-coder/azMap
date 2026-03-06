/* overlay.c — Radio propagation and space weather overlay data parsing/projection.
 *
 * Four overlay types, each with parse → store → project → render pipeline:
 * - MUF: GeoJSON LineStrings with per-feature color/level, split at jumps
 * - Sporadic E: station foEs values → IDW grid → marching squares contours
 * - Aurora: OVATION probability grid → polar mesh with alpha mapping
 * - DRAP: HAF text grid → bilinear lookup → polar mesh with alpha mapping
 *
 * MUF and Sporadic E share the MufData struct; Aurora and DRAP share the
 * AuroraMesh struct (same polar mesh generation pattern as nightmesh). */

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

/* ── Sporadic E (foEs) contour overlay ─────────────────────────── */

/* IDW grid dimensions: 2° resolution */
#define SPORE_GRID_COLS 180   /* -180 to +178 in 2° steps */
#define SPORE_GRID_ROWS  91   /* -90 to +90 in 2° steps */
#define SPORE_MAX_STATIONS 200
#define SPORE_MAX_RADIUS_KM 2500.0

/* Contour levels and colors for foEs */
static const float spore_levels[] = { 3.0f, 5.0f, 7.0f, 10.0f, 14.0f };
static const float spore_colors[][4] = {
    { 0.267f, 0.533f, 1.0f, 1.0f },  /* 3 MHz  #4488ff blue   */
    { 0.267f, 0.8f,   0.267f, 1.0f }, /* 5 MHz  #44cc44 green  */
    { 0.8f,   0.8f,   0.0f,   1.0f }, /* 7 MHz  #cccc00 yellow */
    { 1.0f,   0.533f, 0.0f,   1.0f }, /* 10 MHz #ff8800 orange */
    { 1.0f,   0.133f, 0.133f, 1.0f }, /* 14 MHz #ff2222 red    */
};
#define SPORE_NUM_LEVELS 5

/* Haversine distance in km (inlined for IDW loop performance) */
static double spore_dist_km(double lat1, double lon1, double lat2, double lon2)
{
    double dlat = (lat2 - lat1) * M_PI / 180.0;
    double dlon = (lon2 - lon1) * M_PI / 180.0;
    double a = sin(dlat / 2);
    double b = sin(dlon / 2);
    a = a * a + cos(lat1 * M_PI / 180.0) * cos(lat2 * M_PI / 180.0) * b * b;
    return 2.0 * 6371.0 * asin(sqrt(a));
}

/* Marching squares edge table: for each of the 16 cases, pairs of edges to connect.
 * Edges: 0=bottom, 1=right, 2=top, 3=left. -1 terminates. */
static const int ms_edges[16][5] = {
    { -1, -1, -1, -1, -1 },  /* 0000 */
    {  3,  0, -1, -1, -1 },  /* 0001 */
    {  0,  1, -1, -1, -1 },  /* 0010 */
    {  3,  1, -1, -1, -1 },  /* 0011 */
    {  1,  2, -1, -1, -1 },  /* 0100 */
    { -1, -1, -1, -1, -1 },  /* 0101 saddle — skip */
    {  0,  2, -1, -1, -1 },  /* 0110 */
    {  3,  2, -1, -1, -1 },  /* 0111 */
    {  2,  3, -1, -1, -1 },  /* 1000 */
    {  2,  0, -1, -1, -1 },  /* 1001 */
    { -1, -1, -1, -1, -1 },  /* 1010 saddle — skip */
    {  2,  1, -1, -1, -1 },  /* 1011 */
    {  1,  3, -1, -1, -1 },  /* 1100 */
    {  1,  0, -1, -1, -1 },  /* 1101 */
    {  0,  3, -1, -1, -1 },  /* 1110 */
    { -1, -1, -1, -1, -1 },  /* 1111 */
};

/* Interpolate edge crossing position. Returns lat,lon of the crossing point. */
static void ms_edge_interp(int edge, int r, int c,
                            float v00, float v10, float v01, float v11,
                            float level, double *lat, double *lon)
{
    /* Grid cell corners in lat/lon:
     * (r,c)=bottom-left  (r,c+1)=bottom-right
     * (r+1,c)=top-left   (r+1,c+1)=top-right */
    double lat0 = -90.0 + r * 2.0;
    double lat1 = lat0 + 2.0;
    double lon0 = -180.0 + c * 2.0;
    double lon1 = lon0 + 2.0;

    float t;
    switch (edge) {
    case 0: /* bottom: (r,c)→(r,c+1) */
        t = (v00 == v10) ? 0.5f : (level - v00) / (v10 - v00);
        *lat = lat0;
        *lon = lon0 + t * 2.0;
        break;
    case 1: /* right: (r,c+1)→(r+1,c+1) */
        t = (v10 == v11) ? 0.5f : (level - v10) / (v11 - v10);
        *lat = lat0 + t * 2.0;
        *lon = lon1;
        break;
    case 2: /* top: (r+1,c)→(r+1,c+1) */
        t = (v01 == v11) ? 0.5f : (level - v01) / (v11 - v01);
        *lat = lat1;
        *lon = lon0 + t * 2.0;
        break;
    case 3: /* left: (r,c)→(r+1,c) */
        t = (v00 == v01) ? 0.5f : (level - v00) / (v01 - v00);
        *lat = lat0 + t * 2.0;
        *lon = lon0;
        break;
    default:
        *lat = lat0;
        *lon = lon0;
        break;
    }
}

/* Parse KC2G stations JSON into Sporadic E contour lines.
 *
 * Pipeline: 1) Extract ionosonde stations with valid foEs readings
 *           2) IDW interpolation onto a 2° regular grid (power=2, radius 2500 km)
 *           3) Marching squares to extract contour line fragments
 *           4) Chain fragments into polylines by endpoint matching
 *           5) Project into km-space via muf_reproject() */
int spore_parse_json(const char *json_str, MufData *m)
{
    cJSON *root = cJSON_Parse(json_str);
    if (!root || !cJSON_IsArray(root)) { cJSON_Delete(root); return -1; }

    /* Step 1: Extract stations with valid foEs */
    double sta_lat[SPORE_MAX_STATIONS], sta_lon[SPORE_MAX_STATIONS];
    float sta_foes[SPORE_MAX_STATIONS];
    int nsta = 0;

    cJSON *item;
    cJSON_ArrayForEach(item, root) {
        if (nsta >= SPORE_MAX_STATIONS) break;
        cJSON *foes = cJSON_GetObjectItem(item, "foes");
        if (!foes || cJSON_IsNull(foes) || !cJSON_IsNumber(foes)) continue;
        if (foes->valuedouble <= 0.0) continue;

        /* lat/lon are inside nested "station" object, as strings, lon in 0-360 */
        cJSON *station = cJSON_GetObjectItem(item, "station");
        if (!station) continue;
        cJSON *lat = cJSON_GetObjectItem(station, "latitude");
        cJSON *lon = cJSON_GetObjectItem(station, "longitude");
        if (!lat || !lon) continue;

        double dlat = cJSON_IsNumber(lat) ? lat->valuedouble
                    : (cJSON_IsString(lat) ? strtod(lat->valuestring, NULL) : 0.0);
        double dlon = cJSON_IsNumber(lon) ? lon->valuedouble
                    : (cJSON_IsString(lon) ? strtod(lon->valuestring, NULL) : 0.0);
        if (dlat < -90.0 || dlat > 90.0 || dlon < -360.0 || dlon > 360.0) continue;
        /* Convert 0-360 longitude to -180..+180 */
        if (dlon > 180.0) dlon -= 360.0;

        sta_lat[nsta] = dlat;
        sta_lon[nsta] = dlon;
        sta_foes[nsta] = (float)foes->valuedouble;
        nsta++;
    }
    cJSON_Delete(root);

    if (nsta < 3) return -1; /* not enough data */

    /* Step 2: IDW (Inverse Distance Weighting) interpolation to regular grid.
     * For each grid cell, weight = 1/d² where d = haversine distance to station.
     * Only stations within SPORE_MAX_RADIUS_KM contribute. */
    int grid_sz = SPORE_GRID_ROWS * SPORE_GRID_COLS;
    float *grid = malloc(grid_sz * sizeof(float));
    int *grid_valid = calloc(grid_sz, sizeof(int));
    if (!grid || !grid_valid) { free(grid); free(grid_valid); return -1; }

    for (int r = 0; r < SPORE_GRID_ROWS; r++) {
        double glat = -90.0 + r * 2.0;
        for (int c = 0; c < SPORE_GRID_COLS; c++) {
            double glon = -180.0 + c * 2.0;
            double wsum = 0.0, vsum = 0.0;
            int in_range = 0;

            for (int s = 0; s < nsta; s++) {
                double d = spore_dist_km(glat, glon, sta_lat[s], sta_lon[s]);
                if (d > SPORE_MAX_RADIUS_KM) continue;
                if (d < 1.0) d = 1.0; /* avoid div by zero */
                double w = 1.0 / (d * d);
                wsum += w;
                vsum += w * sta_foes[s];
                in_range = 1;
            }

            int idx = r * SPORE_GRID_COLS + c;
            if (in_range) {
                grid[idx] = (float)(vsum / wsum);
                grid_valid[idx] = 1;
            } else {
                grid[idx] = 0.0f;
            }
        }
    }

    /* Step 3: Marching squares — collect raw edge-crossing fragments.
     * For each 2×2 cell, classify corners above/below the contour level
     * into a 4-bit case index.  The ms_edges lookup table maps each case
     * to a pair of edges to connect.  Saddle cases (0101, 1010) are
     * disambiguated using the cell center average. */
    #define MS_MAX_FRAGS 8000
    typedef struct { double lat[2], lon[2]; } MsFrag;
    MsFrag *frags = malloc(MS_MAX_FRAGS * sizeof(MsFrag));
    if (!frags) { free(grid); free(grid_valid); return -1; }

    /* Temporary output buffers — sized for chained polylines */
    int max_pts = 60000;
    double *lats = malloc(max_pts * sizeof(double));
    double *lons = malloc(max_pts * sizeof(double));
    if (!lats || !lons) { free(grid); free(grid_valid); free(frags); free(lats); free(lons); return -1; }

    free(m->raw_lats);
    free(m->raw_lons);
    m->raw_lats = lats;
    m->raw_lons = lons;
    m->raw_count = 0;
    m->raw_num_segments = 0;
    m->legend_count = 0;

    for (int li = 0; li < SPORE_NUM_LEVELS; li++) {
        float level = spore_levels[li];

        /* Add legend entry */
        if (m->legend_count < MUF_MAX_LEGEND) {
            m->legend[m->legend_count].mhz = level;
            memcpy(m->legend[m->legend_count].color, spore_colors[li], sizeof(float) * 4);
            m->legend_count++;
        }

        /* Phase 1: collect all edge-crossing fragments for this level */
        int nfrags = 0;
        for (int r = 0; r < SPORE_GRID_ROWS - 1; r++) {
            for (int c = 0; c < SPORE_GRID_COLS - 1; c++) {
                int i00 = r * SPORE_GRID_COLS + c;
                int i10 = r * SPORE_GRID_COLS + c + 1;
                int i01 = (r + 1) * SPORE_GRID_COLS + c;
                int i11 = (r + 1) * SPORE_GRID_COLS + c + 1;

                if (!grid_valid[i00] || !grid_valid[i10] ||
                    !grid_valid[i01] || !grid_valid[i11]) continue;

                float v00 = grid[i00], v10 = grid[i10];
                float v01 = grid[i01], v11 = grid[i11];

                int ci = 0;
                if (v00 >= level) ci |= 1;
                if (v10 >= level) ci |= 2;
                if (v11 >= level) ci |= 4;
                if (v01 >= level) ci |= 8;

                /* Saddle cases: disambiguate using cell center average */
                if (ci == 5 || ci == 10) {
                    float center = (v00 + v10 + v01 + v11) * 0.25f;
                    int center_above = (center >= level);
                    if (ci == 5) {
                        /* 0101: BL and TR above */
                        if (center_above) {
                            /* Connect: left→top, bottom→right (two segments) */
                            if (nfrags + 1 < MS_MAX_FRAGS) {
                                ms_edge_interp(3, r, c, v00, v10, v01, v11, level,
                                               &frags[nfrags].lat[0], &frags[nfrags].lon[0]);
                                ms_edge_interp(2, r, c, v00, v10, v01, v11, level,
                                               &frags[nfrags].lat[1], &frags[nfrags].lon[1]);
                                nfrags++;
                                ms_edge_interp(0, r, c, v00, v10, v01, v11, level,
                                               &frags[nfrags].lat[0], &frags[nfrags].lon[0]);
                                ms_edge_interp(1, r, c, v00, v10, v01, v11, level,
                                               &frags[nfrags].lat[1], &frags[nfrags].lon[1]);
                                nfrags++;
                            }
                        } else {
                            /* Connect: left→bottom, top→right */
                            if (nfrags + 1 < MS_MAX_FRAGS) {
                                ms_edge_interp(3, r, c, v00, v10, v01, v11, level,
                                               &frags[nfrags].lat[0], &frags[nfrags].lon[0]);
                                ms_edge_interp(0, r, c, v00, v10, v01, v11, level,
                                               &frags[nfrags].lat[1], &frags[nfrags].lon[1]);
                                nfrags++;
                                ms_edge_interp(2, r, c, v00, v10, v01, v11, level,
                                               &frags[nfrags].lat[0], &frags[nfrags].lon[0]);
                                ms_edge_interp(1, r, c, v00, v10, v01, v11, level,
                                               &frags[nfrags].lat[1], &frags[nfrags].lon[1]);
                                nfrags++;
                            }
                        }
                    } else { /* ci == 10: BR and TL above */
                        if (center_above) {
                            if (nfrags + 1 < MS_MAX_FRAGS) {
                                ms_edge_interp(0, r, c, v00, v10, v01, v11, level,
                                               &frags[nfrags].lat[0], &frags[nfrags].lon[0]);
                                ms_edge_interp(3, r, c, v00, v10, v01, v11, level,
                                               &frags[nfrags].lat[1], &frags[nfrags].lon[1]);
                                nfrags++;
                                ms_edge_interp(1, r, c, v00, v10, v01, v11, level,
                                               &frags[nfrags].lat[0], &frags[nfrags].lon[0]);
                                ms_edge_interp(2, r, c, v00, v10, v01, v11, level,
                                               &frags[nfrags].lat[1], &frags[nfrags].lon[1]);
                                nfrags++;
                            }
                        } else {
                            if (nfrags + 1 < MS_MAX_FRAGS) {
                                ms_edge_interp(0, r, c, v00, v10, v01, v11, level,
                                               &frags[nfrags].lat[0], &frags[nfrags].lon[0]);
                                ms_edge_interp(1, r, c, v00, v10, v01, v11, level,
                                               &frags[nfrags].lat[1], &frags[nfrags].lon[1]);
                                nfrags++;
                                ms_edge_interp(2, r, c, v00, v10, v01, v11, level,
                                               &frags[nfrags].lat[0], &frags[nfrags].lon[0]);
                                ms_edge_interp(3, r, c, v00, v10, v01, v11, level,
                                               &frags[nfrags].lat[1], &frags[nfrags].lon[1]);
                                nfrags++;
                            }
                        }
                    }
                    continue;
                }

                if (ms_edges[ci][0] < 0) continue;
                if (nfrags >= MS_MAX_FRAGS) break;

                ms_edge_interp(ms_edges[ci][0], r, c, v00, v10, v01, v11, level,
                               &frags[nfrags].lat[0], &frags[nfrags].lon[0]);
                ms_edge_interp(ms_edges[ci][1], r, c, v00, v10, v01, v11, level,
                               &frags[nfrags].lat[1], &frags[nfrags].lon[1]);
                nfrags++;
            }
        }

        /* Step 4: Chain fragments into polylines by endpoint matching.
         * Two fragment endpoints match if within MS_EPS (~1°, half a grid cell).
         * For each unchained seed fragment, extend forward (match tail) and
         * backward (match head, prepending via memmove) greedily. */
        #define MS_EPS 1.01  /* slightly over 1° — half of 2° grid */
        int *used = calloc(nfrags, sizeof(int));
        if (!used) continue;

        for (int seed = 0; seed < nfrags; seed++) {
            if (used[seed]) continue;
            if (m->raw_num_segments >= MUF_MAX_SEGMENTS) break;

            /* Start a new chain from this fragment */
            used[seed] = 1;
            int seg_start = m->raw_count;

            /* Add seed's two points */
            if (m->raw_count + 2 > max_pts) break;
            lats[m->raw_count] = frags[seed].lat[0];
            lons[m->raw_count] = frags[seed].lon[0];
            m->raw_count++;
            lats[m->raw_count] = frags[seed].lat[1];
            lons[m->raw_count] = frags[seed].lon[1];
            m->raw_count++;

            /* Extend forward (match tail) */
            int found;
            do {
                found = 0;
                double tlat = lats[m->raw_count - 1];
                double tlon = lons[m->raw_count - 1];
                for (int f = 0; f < nfrags; f++) {
                    if (used[f]) continue;
                    if (m->raw_count + 1 > max_pts) break;
                    /* Try matching fragment's start to our tail */
                    if (fabs(frags[f].lat[0] - tlat) < MS_EPS &&
                        fabs(frags[f].lon[0] - tlon) < MS_EPS) {
                        lats[m->raw_count] = frags[f].lat[1];
                        lons[m->raw_count] = frags[f].lon[1];
                        m->raw_count++;
                        used[f] = 1;
                        found = 1;
                        break;
                    }
                    /* Try matching fragment's end to our tail (reverse) */
                    if (fabs(frags[f].lat[1] - tlat) < MS_EPS &&
                        fabs(frags[f].lon[1] - tlon) < MS_EPS) {
                        lats[m->raw_count] = frags[f].lat[0];
                        lons[m->raw_count] = frags[f].lon[0];
                        m->raw_count++;
                        used[f] = 1;
                        found = 1;
                        break;
                    }
                }
            } while (found);

            /* Extend backward (match head) */
            do {
                found = 0;
                double hlat = lats[seg_start];
                double hlon = lons[seg_start];
                for (int f = 0; f < nfrags; f++) {
                    if (used[f]) continue;
                    double append_lat, append_lon;
                    int matched = 0;
                    if (fabs(frags[f].lat[1] - hlat) < MS_EPS &&
                        fabs(frags[f].lon[1] - hlon) < MS_EPS) {
                        append_lat = frags[f].lat[0];
                        append_lon = frags[f].lon[0];
                        matched = 1;
                    } else if (fabs(frags[f].lat[0] - hlat) < MS_EPS &&
                               fabs(frags[f].lon[0] - hlon) < MS_EPS) {
                        append_lat = frags[f].lat[1];
                        append_lon = frags[f].lon[1];
                        matched = 1;
                    }
                    if (matched) {
                        if (m->raw_count + 1 > max_pts) break;
                        /* Shift everything right by 1 to prepend */
                        int chain_len = m->raw_count - seg_start;
                        memmove(&lats[seg_start + 1], &lats[seg_start],
                                chain_len * sizeof(double));
                        memmove(&lons[seg_start + 1], &lons[seg_start],
                                chain_len * sizeof(double));
                        lats[seg_start] = append_lat;
                        lons[seg_start] = append_lon;
                        m->raw_count++;
                        used[f] = 1;
                        found = 1;
                        break;
                    }
                }
            } while (found);

            int seg_len = m->raw_count - seg_start;
            if (seg_len >= 2 && m->raw_num_segments < MUF_MAX_SEGMENTS) {
                int si = m->raw_num_segments;
                m->raw_seg_starts[si] = seg_start;
                m->raw_seg_counts[si] = seg_len;
                memcpy(m->raw_seg_colors[si], spore_colors[li], sizeof(float) * 4);
                m->raw_num_segments++;
            }
        }

        free(used);
        #undef MS_EPS
    }

    free(frags);
    free(grid_valid);
    free(grid);
    #undef MS_MAX_FRAGS

    /* Project into km-space */
    muf_reproject(m);
    return 0;
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
        cJSON *j0 = cJSON_GetArrayItem(triplet, 0);
        cJSON *j1 = cJSON_GetArrayItem(triplet, 1);
        cJSON *j2 = cJSON_GetArrayItem(triplet, 2);
        if (!j0 || !j1 || !j2) continue;
        int lon = (int)j0->valuedouble;
        int lat = (int)j1->valuedouble;
        int val = (int)j2->valuedouble;

        /* Normalize lon to 0-359 */
        lon = ((lon % 360) + 360) % 360;
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

/* ── DRAP (D-Region Absorption Prediction) ─────────────────────── */

void drap_grid_init(DrapGrid *g)
{
    g->values = NULL;
    g->peak_mhz = 0.0f;
    g->valid = 0;
}

void drap_grid_free(DrapGrid *g)
{
    free(g->values);
    g->values = NULL;
    g->peak_mhz = 0.0f;
    g->valid = 0;
}

/* Parse NOAA DRAP text file: comment lines (#), longitude header row,
 * then data rows with latitude | frequency values at 4°×2° resolution. */
int drap_parse_text(const char *text, DrapGrid *g)
{
    if (!text) return -1;

    int grid_sz = DRAP_GRID_ROWS * DRAP_GRID_COLS;
    if (!g->values) {
        g->values = malloc(grid_sz * sizeof(float));
        if (!g->values) return -1;
    }
    memset(g->values, 0, grid_sz * sizeof(float));

    const char *p = text;
    int row = 0;

    while (*p && row < DRAP_GRID_ROWS) {
        /* Skip to start of line */
        while (*p == ' ' || *p == '\t') p++;

        /* Skip comment lines */
        if (*p == '#') {
            while (*p && *p != '\n') p++;
            if (*p == '\n') p++;
            continue;
        }
        /* Skip empty lines */
        if (*p == '\n') { p++; continue; }
        if (*p == '\0') break;

        /* First non-comment line is the longitude header — skip it */
        /* Detect: first token is not a number with optional sign that looks like a latitude */
        char *end;
        double first = strtod(p, &end);
        if (end == p) {
            /* Not a number, skip line */
            while (*p && *p != '\n') p++;
            if (*p == '\n') p++;
            continue;
        }

        /* Longitude header: first value is a longitude (typically -178),
         * data rows: first value is a latitude (89, 87, ..., -89).
         * Latitude of row 0 is 89. We detect the header by checking
         * if we haven't started collecting rows yet AND the first value
         * looks like -178 (a longitude, not 89). */
        if (row == 0 && first < -90.0) {
            /* This is the longitude header, skip */
            while (*p && *p != '\n') p++;
            if (*p == '\n') p++;
            continue;
        }

        /* Data row: latitude | frequency values */
        /* Skip the latitude value */
        p = end;

        /* Skip optional pipe delimiter */
        while (*p == ' ' || *p == '\t') p++;
        if (*p == '|') p++;

        /* Read frequency values */
        int col = 0;
        while (*p && *p != '\n' && col < DRAP_GRID_COLS) {
            while (*p == ' ' || *p == '\t') p++;
            if (*p == '\n' || *p == '\0') break;
            float val = (float)strtod(p, &end);
            if (end == p) break;
            p = end;
            g->values[row * DRAP_GRID_COLS + col] = val;
            col++;
        }

        row++;
        /* Skip to next line */
        while (*p && *p != '\n') p++;
        if (*p == '\n') p++;
    }

    if (row < DRAP_GRID_ROWS) return -1;

    /* Find peak */
    g->peak_mhz = 0.0f;
    for (int i = 0; i < grid_sz; i++) {
        if (g->values[i] > g->peak_mhz)
            g->peak_mhz = g->values[i];
    }

    g->valid = 1;
    return 0;
}

/* Bilinear lookup of DRAP HAF at lat/lon, returns alpha 0-1 */
static float drap_lookup(const DrapGrid *g, double lat, double lon)
{
    /* Normalize longitude to -178..178 range */
    if (lon > 180.0) lon -= 360.0;
    if (lon < -180.0) lon += 360.0;

    /* Grid indices (continuous) */
    double col_f = (lon - (-178.0)) / 4.0;
    double row_f = (89.0 - lat) / 2.0;

    /* Clamp to grid bounds */
    if (col_f < 0.0) col_f = 0.0;
    if (col_f > DRAP_GRID_COLS - 1.001) col_f = DRAP_GRID_COLS - 1.001;
    if (row_f < 0.0) row_f = 0.0;
    if (row_f > DRAP_GRID_ROWS - 1.001) row_f = DRAP_GRID_ROWS - 1.001;

    int c0 = (int)col_f, r0 = (int)row_f;
    int c1 = c0 + 1, r1 = r0 + 1;
    if (c1 >= DRAP_GRID_COLS) c1 = DRAP_GRID_COLS - 1;
    if (r1 >= DRAP_GRID_ROWS) r1 = DRAP_GRID_ROWS - 1;

    float fc = (float)(col_f - c0);
    float fr = (float)(row_f - r0);

    float v00 = g->values[r0 * DRAP_GRID_COLS + c0];
    float v10 = g->values[r0 * DRAP_GRID_COLS + c1];
    float v01 = g->values[r1 * DRAP_GRID_COLS + c0];
    float v11 = g->values[r1 * DRAP_GRID_COLS + c1];

    float val = v00 * (1 - fc) * (1 - fr) + v10 * fc * (1 - fr)
              + v01 * (1 - fc) * fr + v11 * fc * fr;

    /* Map HAF (MHz) to alpha:
     * 0-0.5 → 0 (baseline noise, ignore)
     * 0.5-3 → ramp 0.0 to 0.2  (quiet-time absorption)
     * 3-10  → ramp 0.2 to 0.45 (moderate event)
     * 10-30 → ramp 0.45 to 0.7 (major event / blackout) */
    if (val <= 0.5f) return 0.0f;
    if (val <= 3.0f) return (val - 0.5f) / 2.5f * 0.2f;
    if (val <= 10.0f) return 0.2f + (val - 3.0f) / 7.0f * 0.25f;
    return 0.45f + (val - 10.0f) / 20.0f * 0.25f;
}

void drap_mesh_build(AuroraMesh *m, const DrapGrid *g)
{
    if (!g || !g->valid || !m->vertices) return;

    #define DRAP_ANGULAR_DIVS  180
    #define DRAP_RADIAL_DIVS    60

    double max_r = projection_get_radius() - 0.5;
    float dr = (float)(max_r / DRAP_RADIAL_DIVS);
    float da = 2.0f * (float)M_PI / DRAP_ANGULAR_DIVS;

    m->vertex_count = 0;

    /* Precompute alpha grid */
    int rows = DRAP_RADIAL_DIVS + 1;
    int cols = DRAP_ANGULAR_DIVS;
    float *alpha_grid = malloc(rows * cols * sizeof(float));
    if (!alpha_grid) return;

    for (int ri = 0; ri <= DRAP_RADIAL_DIVS; ri++) {
        float r = ri * dr;
        for (int ai = 0; ai < DRAP_ANGULAR_DIVS; ai++) {
            float a = ai * da;
            float x = r * cosf(a);
            float y = r * sinf(a);
            float av = 0.0f;

            double lat, lon;
            if (ri == 0) {
                if (projection_inverse(0.0, 0.0, &lat, &lon) == 0)
                    av = drap_lookup(g, lat, lon);
            } else {
                if (projection_inverse((double)x, (double)y, &lat, &lon) == 0)
                    av = drap_lookup(g, lat, lon);
            }
            alpha_grid[ri * cols + ai] = av;
        }
    }

    /* Generate triangles — same pattern as aurora/nightmesh */
    float center_alpha = alpha_grid[0];

    for (int ai = 0; ai < DRAP_ANGULAR_DIVS; ai++) {
        int ai_next = (ai + 1) % DRAP_ANGULAR_DIVS;
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
        for (int ri = 1; ri < DRAP_RADIAL_DIVS; ri++) {
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

    #undef DRAP_ANGULAR_DIVS
    #undef DRAP_RADIAL_DIVS
}

/* ── Geomagnetic indices (Kp + Bz) ────────────────────────────── */

void geomag_init(GeomagIndices *g)
{
    g->kp = 0.0f;
    g->bz = 0.0f;
    g->valid = 0;
}

int geomag_parse_kp(const char *json_str, GeomagIndices *g)
{
    /* Format: array of arrays, first row is header, last row is most recent.
     * Each row: ["time_tag", "Kp", "a_running", "station_count"] */
    cJSON *root = cJSON_Parse(json_str);
    if (!root || !cJSON_IsArray(root)) { cJSON_Delete(root); return -1; }

    int n = cJSON_GetArraySize(root);
    if (n < 2) { cJSON_Delete(root); return -1; } /* need header + at least 1 data row */

    /* Last entry is the most recent */
    cJSON *last = cJSON_GetArrayItem(root, n - 1);
    if (!last || !cJSON_IsArray(last) || cJSON_GetArraySize(last) < 2) {
        cJSON_Delete(root);
        return -1;
    }

    cJSON *kp_val = cJSON_GetArrayItem(last, 1);
    if (kp_val && cJSON_IsString(kp_val))
        g->kp = (float)atof(kp_val->valuestring);
    else if (kp_val && cJSON_IsNumber(kp_val))
        g->kp = (float)kp_val->valuedouble;

    g->valid = 1;
    cJSON_Delete(root);
    return 0;
}

int geomag_parse_bz(const char *json_str, GeomagIndices *g)
{
    /* Format: {"Bt": "5", "Bz": "-2", "TimeStamp": "..."} */
    cJSON *root = cJSON_Parse(json_str);
    if (!root) return -1;

    cJSON *bz_val = cJSON_GetObjectItem(root, "Bz");
    if (bz_val && cJSON_IsString(bz_val))
        g->bz = (float)atof(bz_val->valuestring);
    else if (bz_val && cJSON_IsNumber(bz_val))
        g->bz = (float)bz_val->valuedouble;

    g->valid = 1;
    cJSON_Delete(root);
    return 0;
}
