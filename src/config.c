#include "config.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

/* Trim leading and trailing whitespace in-place, return pointer into buf. */
static char *trim(char *s)
{
    while (isspace((unsigned char)*s)) s++;
    if (*s == '\0') return s;
    char *end = s + strlen(s) - 1;
    while (end > s && isspace((unsigned char)*end)) *end-- = '\0';
    return s;
}

static void get_config_path(char *out, size_t sz)
{
    const char *home = getenv("HOME");
    if (home)
        snprintf(out, sz, "%s/.config/azmap.conf", home);
    else
        out[0] = '\0';
}

int config_load(Config *cfg)
{
    memset(cfg, 0, sizeof(*cfg));

    char path[1024];
    get_config_path(path, sizeof(path));
    if (!path[0]) return -1;

    FILE *f = fopen(path, "r");
    if (!f) return -1;

    int has_lat = 0, has_lon = 0;
    int has_tlat = 0, has_tlon = 0;
    int has_vzoom = 0, has_vproj = 0, has_vclat = 0, has_vclon = 0;
    char line[512];
    while (fgets(line, sizeof(line), f)) {
        line[strcspn(line, "\r\n")] = '\0';

        char *s = trim(line);
        if (*s == '\0' || *s == '#') continue;

        char *eq = strchr(s, '=');
        if (!eq) continue;

        *eq = '\0';
        char *key = trim(s);
        char *val = trim(eq + 1);

        if (strcmp(key, "name") == 0) {
            strncpy(cfg->name, val, sizeof(cfg->name) - 1);
        } else if (strcmp(key, "lat") == 0) {
            cfg->lat = atof(val);
            has_lat = 1;
        } else if (strcmp(key, "lon") == 0) {
            cfg->lon = atof(val);
            has_lon = 1;
        } else if (strcmp(key, "qrz_user") == 0) {
            strncpy(cfg->qrz_user, val, sizeof(cfg->qrz_user) - 1);
        } else if (strcmp(key, "qrz_pass") == 0) {
            strncpy(cfg->qrz_pass, val, sizeof(cfg->qrz_pass) - 1);
        } else if (strcmp(key, "target_lat") == 0) {
            cfg->target_lat = atof(val);
            has_tlat = 1;
        } else if (strcmp(key, "target_lon") == 0) {
            cfg->target_lon = atof(val);
            has_tlon = 1;
        } else if (strcmp(key, "target_name") == 0) {
            strncpy(cfg->target_name, val, sizeof(cfg->target_name) - 1);
        } else if (strcmp(key, "view_zoom_km") == 0) {
            cfg->view_zoom_km = (float)atof(val);
            has_vzoom = 1;
        } else if (strcmp(key, "view_pan_x") == 0) {
            cfg->view_pan_x = (float)atof(val);
        } else if (strcmp(key, "view_pan_y") == 0) {
            cfg->view_pan_y = (float)atof(val);
        } else if (strcmp(key, "view_proj_mode") == 0) {
            if (strcmp(val, "ortho") == 0)
                cfg->view_proj_mode = 1;
            else
                cfg->view_proj_mode = 0;
            has_vproj = 1;
        } else if (strcmp(key, "view_center_lat") == 0) {
            cfg->view_center_lat = atof(val);
            has_vclat = 1;
        } else if (strcmp(key, "view_center_lon") == 0) {
            cfg->view_center_lon = atof(val);
            has_vclon = 1;
        } else if (strcmp(key, "window_w") == 0) {
            cfg->window_w = atoi(val);
        } else if (strcmp(key, "window_h") == 0) {
            cfg->window_h = atoi(val);
        } else if (strcmp(key, "panel_visible") == 0) {
            cfg->panel_visible = atoi(val);
        }
    }

    fclose(f);

    if (has_lat && has_lon)
        cfg->valid = 1;
    if (has_tlat && has_tlon)
        cfg->target_valid = 1;
    if (has_vzoom && has_vproj && has_vclat && has_vclon)
        cfg->view_valid = 1;
    if (cfg->window_w > 0 && cfg->window_h > 0)
        cfg->window_valid = 1;

    return 0;
}

/* State keys we manage in the config file */
#define NUM_STATE_KEYS 12
static const char *state_keys[] = {
    "target_lat", "target_lon", "target_name",
    "view_zoom_km", "view_pan_x", "view_pan_y",
    "view_proj_mode", "view_center_lat", "view_center_lon",
    "window_w", "window_h", "panel_visible",
    NULL
};

int config_save_state(double target_lat, double target_lon, const char *target_name,
                      float zoom_km, float pan_x, float pan_y,
                      int proj_mode, double center_lat, double center_lon,
                      int window_w, int window_h, int panel_visible)
{
    char path[1024];
    get_config_path(path, sizeof(path));
    if (!path[0]) return -1;

    /* Read existing lines */
    #define MAX_LINES 256
    char lines[MAX_LINES][512];
    int nlines = 0;
    int key_written[NUM_STATE_KEYS] = {0};

    FILE *f = fopen(path, "r");
    if (f) {
        while (nlines < MAX_LINES && fgets(lines[nlines], sizeof(lines[0]), f)) {
            lines[nlines][strcspn(lines[nlines], "\r\n")] = '\0';
            nlines++;
        }
        fclose(f);
    }

    /* Build new values */
    char new_vals[NUM_STATE_KEYS][256];
    snprintf(new_vals[0], sizeof(new_vals[0]), "target_lat = %.6f", target_lat);
    snprintf(new_vals[1], sizeof(new_vals[1]), "target_lon = %.6f", target_lon);
    snprintf(new_vals[2], sizeof(new_vals[2]), "target_name = %s", target_name ? target_name : "");
    snprintf(new_vals[3], sizeof(new_vals[3]), "view_zoom_km = %.2f", zoom_km);
    snprintf(new_vals[4], sizeof(new_vals[4]), "view_pan_x = %.2f", pan_x);
    snprintf(new_vals[5], sizeof(new_vals[5]), "view_pan_y = %.2f", pan_y);
    snprintf(new_vals[6], sizeof(new_vals[6]), "view_proj_mode = %s", proj_mode == 1 ? "ortho" : "azeq");
    snprintf(new_vals[7], sizeof(new_vals[7]), "view_center_lat = %.6f", center_lat);
    snprintf(new_vals[8], sizeof(new_vals[8]), "view_center_lon = %.6f", center_lon);
    snprintf(new_vals[9], sizeof(new_vals[9]), "window_w = %d", window_w);
    snprintf(new_vals[10], sizeof(new_vals[10]), "window_h = %d", window_h);
    snprintf(new_vals[11], sizeof(new_vals[11]), "panel_visible = %d", panel_visible);

    /* Replace existing state key lines */
    for (int li = 0; li < nlines; li++) {
        char tmp[512];
        strncpy(tmp, lines[li], sizeof(tmp) - 1);
        tmp[sizeof(tmp) - 1] = '\0';
        char *s = trim(tmp);
        if (*s == '\0' || *s == '#') continue;
        char *eq = strchr(s, '=');
        if (!eq) continue;
        *eq = '\0';
        char *key = trim(s);
        for (int ki = 0; state_keys[ki]; ki++) {
            if (strcmp(key, state_keys[ki]) == 0) {
                strncpy(lines[li], new_vals[ki], sizeof(lines[0]) - 1);
                lines[li][sizeof(lines[0]) - 1] = '\0';
                key_written[ki] = 1;
                break;
            }
        }
    }

    /* Append any keys not already in file */
    int need_section = 0;
    for (int ki = 0; state_keys[ki]; ki++) {
        if (!key_written[ki]) { need_section = 1; break; }
    }
    if (need_section && nlines > 0 && lines[nlines - 1][0] != '\0' && nlines < MAX_LINES) {
        lines[nlines][0] = '\0'; /* blank separator */
        nlines++;
    }
    for (int ki = 0; state_keys[ki]; ki++) {
        if (!key_written[ki] && nlines < MAX_LINES) {
            strncpy(lines[nlines], new_vals[ki], sizeof(lines[0]) - 1);
            lines[nlines][sizeof(lines[0]) - 1] = '\0';
            nlines++;
        }
    }

    /* Write back */
    f = fopen(path, "w");
    if (!f) return -1;
    for (int li = 0; li < nlines; li++)
        fprintf(f, "%s\n", lines[li]);
    fclose(f);

    return 0;
    #undef MAX_LINES
}
