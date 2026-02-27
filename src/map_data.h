#ifndef MAP_DATA_H
#define MAP_DATA_H

#define MAX_SEGMENTS 4096

typedef struct {
    float *vertices;       /* Interleaved x,y pairs in km (projected) */
    int    vertex_count;   /* Total number of vertices */
    int    segment_starts[MAX_SEGMENTS]; /* Start index of each polyline */
    int    segment_counts[MAX_SEGMENTS]; /* Vertex count per polyline */
    int    num_segments;
    /* Raw lat/lon for reprojection */
    double *raw_lats;
    double *raw_lons;
    int     raw_count;
    int     raw_seg_starts[MAX_SEGMENTS];
    int     raw_seg_counts[MAX_SEGMENTS];
    int     raw_num_segments;
} MapData;

/* Load shapefile and project all vertices. Returns 0 on success. */
int map_data_load(MapData *md, const char *shp_path);

/* Re-project all vertices (call after changing projection center). */
void map_data_reproject(MapData *md, const char *shp_path);

/* Free allocated memory. */
void map_data_free(MapData *md);

#endif
