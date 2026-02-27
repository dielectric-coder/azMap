#include "config.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

/* Trim leading and trailing whitespace in-place, return pointer into buf. */
static char *trim(char *s)
{
    while (isspace((unsigned char)*s)) s++;
    char *end = s + strlen(s) - 1;
    while (end > s && isspace((unsigned char)*end)) *end-- = '\0';
    return s;
}

int config_load(Config *cfg)
{
    cfg->name[0] = '\0';
    cfg->lat = 0.0;
    cfg->lon = 0.0;
    cfg->valid = 0;
    cfg->qrz_user[0] = '\0';
    cfg->qrz_pass[0] = '\0';

    const char *home = getenv("HOME");
    if (!home) return -1;

    char path[1024];
    snprintf(path, sizeof(path), "%s/.config/azmap.conf", home);

    FILE *f = fopen(path, "r");
    if (!f) return -1;

    int has_lat = 0, has_lon = 0;
    char line[512];
    while (fgets(line, sizeof(line), f)) {
        /* Strip newline */
        line[strcspn(line, "\r\n")] = '\0';

        char *s = trim(line);
        if (*s == '\0' || *s == '#') continue;

        /* Split on first '=' */
        char *eq = strchr(s, '=');
        if (!eq) continue;

        *eq = '\0';
        char *key = trim(s);
        char *val = trim(eq + 1);

        if (strcmp(key, "name") == 0) {
            strncpy(cfg->name, val, sizeof(cfg->name) - 1);
            cfg->name[sizeof(cfg->name) - 1] = '\0';
        } else if (strcmp(key, "lat") == 0) {
            cfg->lat = atof(val);
            has_lat = 1;
        } else if (strcmp(key, "lon") == 0) {
            cfg->lon = atof(val);
            has_lon = 1;
        } else if (strcmp(key, "qrz_user") == 0) {
            strncpy(cfg->qrz_user, val, sizeof(cfg->qrz_user) - 1);
            cfg->qrz_user[sizeof(cfg->qrz_user) - 1] = '\0';
        } else if (strcmp(key, "qrz_pass") == 0) {
            strncpy(cfg->qrz_pass, val, sizeof(cfg->qrz_pass) - 1);
            cfg->qrz_pass[sizeof(cfg->qrz_pass) - 1] = '\0';
        }
    }

    fclose(f);

    if (has_lat && has_lon)
        cfg->valid = 1;

    return 0;
}
