#ifndef OVERLAY_H
#define OVERLAY_H

#define OVERLAY_UPDATE_SEC  900  /* 15 minutes */
#define MUF_URL    "https://prop.kc2g.com/renders/current/mufd-normal-now.geojson"
#define AURORA_URL "https://services.swpc.noaa.gov/json/ovation_aurora_latest.json"

#define MUF_MAX_SEGMENTS 128
#define MUF_MAX_LEGEND   16

/* MUF legend entry: MHz level + color */
typedef struct {
    float mhz;
    float color[4]; /* RGBA */
} MufLegendEntry;

/* MUF contour line data — raw lat/lon + projected vertices with per-segment color */
typedef struct {
    /* Raw lat/lon for reprojection */
    double *raw_lats;
    double *raw_lons;
    int     raw_count;
    int     raw_seg_starts[MUF_MAX_SEGMENTS];
    int     raw_seg_counts[MUF_MAX_SEGMENTS];
    float   raw_seg_colors[MUF_MAX_SEGMENTS][4]; /* RGBA per raw segment */
    int     raw_num_segments;

    /* Projected vertices (after split at large jumps) */
    float  *vertices;              /* x,y pairs in km-space */
    int     vertex_count;
    int     segment_starts[MUF_MAX_SEGMENTS];
    int     segment_counts[MUF_MAX_SEGMENTS];
    float   segment_colors[MUF_MAX_SEGMENTS][4]; /* RGBA per projected segment */
    int     num_segments;

    /* Legend: unique (mhz, color) pairs for sidebar display */
    MufLegendEntry legend[MUF_MAX_LEGEND];
    int            legend_count;
} MufData;

/* Aurora heatmap mesh (same vertex format as NightMesh: x, y, alpha) */
typedef struct {
    float *vertices;    /* interleaved x, y, alpha */
    int    vertex_count;
    int    capacity;
} AuroraMesh;

/* Aurora raw grid (parsed from JSON, used to build mesh) */
typedef struct {
    int    *values;     /* aurora probability 0-100, indexed [lon * 181 + (lat+90)] */
    int     valid;      /* 1 if data loaded successfully */
} AuroraGrid;

void  muf_data_init(MufData *m);
void  muf_data_free(MufData *m);
int   muf_parse_geojson(const char *json_str, MufData *m);
void  muf_reproject(MufData *m);

void  aurora_grid_init(AuroraGrid *g);
void  aurora_grid_free(AuroraGrid *g);
int   aurora_parse_json(const char *json_str, AuroraGrid *g);

void  aurora_mesh_init(AuroraMesh *m);
void  aurora_mesh_free(AuroraMesh *m);
void  aurora_mesh_build(AuroraMesh *m, const AuroraGrid *g);

#endif
