#ifndef CONFIG_H
#define CONFIG_H

typedef struct {
    char name[128];
    double lat;
    double lon;
    int valid; /* 1 if both lat and lon were found */
} Config;

/* Load config from ~/.config/azmap.conf. Returns 0 on success, -1 if not found/error. */
int config_load(Config *cfg);

#endif
