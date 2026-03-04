#ifndef CONFIG_H
#define CONFIG_H

typedef struct {
    char name[128];
    double lat;
    double lon;
    int valid; /* 1 if both lat and lon were found */
    char qrz_user[64];
    char qrz_pass[64];

    /* Persisted target */
    double target_lat, target_lon;
    char   target_name[128];
    int    target_valid;       /* 1 if both target lat/lon found */

    /* Persisted view state */
    float  view_zoom_km;
    float  view_pan_x, view_pan_y;
    int    view_proj_mode;     /* 0=PROJ_AZEQ, 1=PROJ_ORTHO */
    double view_center_lat, view_center_lon;
    int    view_valid;         /* 1 if view state keys found */

    /* Persisted window state */
    int    window_w, window_h; /* window size in screen coords (not framebuffer) */
    int    panel_visible;      /* sidebar panel open/closed */
    int    window_valid;       /* 1 if window_w and window_h found */
} Config;

/* Load config from ~/.config/azmap.conf. Returns 0 on success, -1 if not found/error. */
int config_load(Config *cfg);

/* Save session state (target + view) to ~/.config/azmap.conf using merge-write.
 * Preserves existing comments, ordering, and credentials. Returns 0 on success. */
int config_save_state(double target_lat, double target_lon, const char *target_name,
                      float zoom_km, float pan_x, float pan_y,
                      int proj_mode, double center_lat, double center_lon,
                      int window_w, int window_h, int panel_visible);

#endif
