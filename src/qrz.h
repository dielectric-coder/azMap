#ifndef QRZ_H
#define QRZ_H

typedef struct {
    double lat, lon;
    char call[32];       /* normalized callsign */
    char name[128];      /* "fname name" */
    char location[128];  /* "addr2, country" */
    char grid[16];       /* Maidenhead grid */
    int  valid;          /* 1 if lat/lon were found */
} QRZResult;

/* Initialize with credentials. Returns 0 on success. */
int qrz_init(const char *username, const char *password);

/* Lookup a callsign. Blocks during HTTP request.
 * Returns 0 on success, -1 on error. err_buf filled on error. */
int qrz_lookup(const char *callsign, QRZResult *result, char *err_buf, int err_sz);

/* Free any resources (curl cleanup). */
void qrz_cleanup(void);

#endif
