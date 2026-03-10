/* icon.h — Procedurally generated globe icon for the window.
 *
 * Generates an RGBA pixel buffer depicting a blue globe with green/brown
 * land masses (simplified continental outlines), grid lines, and a subtle
 * atmosphere glow.  Call icon_generate_sz() with any size. */

#ifndef ICON_H
#define ICON_H

#include <math.h>
#include <string.h>

/* Simple continent check: returns 1 if (lat,lon) is roughly over land.
 * Uses bounding-box approximations of major landmasses. */
static int icon_is_land(double lat, double lon)
{
    /* Africa */
    if (lat >= -35 && lat <= 37 && lon >= -18 && lon <= 52) {
        if (lat >= -35 && lat <= 0 && lon >= 15 && lon <= 52) return 1;
        if (lat >= 0 && lat <= 37 && lon >= -18 && lon <= 52) return 1;
    }
    /* Europe */
    if (lat >= 36 && lat <= 71 && lon >= -10 && lon <= 40) return 1;
    /* Asia */
    if (lat >= 10 && lat <= 75 && lon >= 40 && lon <= 145) return 1;
    if (lat >= -10 && lat <= 10 && lon >= 95 && lon <= 140) return 1; /* SE Asia */
    /* North America */
    if (lat >= 15 && lat <= 72 && lon >= -170 && lon <= -50) return 1;
    /* South America */
    if (lat >= -56 && lat <= 15 && lon >= -82 && lon <= -34) return 1;
    /* Australia */
    if (lat >= -45 && lat <= -10 && lon >= 112 && lon <= 155) return 1;
    /* Greenland */
    if (lat >= 60 && lat <= 84 && lon >= -73 && lon <= -12) return 1;
    return 0;
}

/* Generate an sz x sz RGBA icon into buf (must be at least sz*sz*4 bytes). */
static void icon_generate_sz(unsigned char *buf, int sz)
{
    const double cx = (sz - 1) / 2.0;
    const double cy = (sz - 1) / 2.0;
    const double r = sz / 2.0 - 1.0;

    memset(buf, 0, sz * sz * 4);

    for (int y = 0; y < sz; y++) {
        for (int x = 0; x < sz; x++) {
            unsigned char *px = buf + (y * sz + x) * 4;
            double dx = x - cx;
            double dy = y - cy;
            double dist = sqrt(dx * dx + dy * dy);

            if (dist > r + 1.5) {
                continue;
            }

            if (dist > r) {
                double a = 1.0 - (dist - r);
                if (a < 0) a = 0;
                px[0] = 100; px[1] = 160; px[2] = 255;
                px[3] = (unsigned char)(a * 120);
                continue;
            }

            /* Orthographic inverse: pixel to (lat, lon) on sphere */
            double nx = dx / r;
            double ny = -dy / r;
            double nz2 = 1.0 - nx * nx - ny * ny;
            if (nz2 < 0) nz2 = 0;
            double nz = sqrt(nz2);

            double lat = asin(ny) * (180.0 / M_PI);
            double lon = atan2(nx, nz) * (180.0 / M_PI) - 20.0;
            if (lon < -180) lon += 360;
            if (lon > 180) lon -= 360;

            /* Lighting: simple diffuse from upper-left */
            double light = 0.4 + 0.6 * fmax(0, 0.5 * nx + 0.7 * ny + 0.5 * nz);

            if (icon_is_land(lat, lon)) {
                double g = 0.45 + 0.15 * (lat / 90.0);
                px[0] = (unsigned char)(65 * light);
                px[1] = (unsigned char)(140 * g * light);
                px[2] = (unsigned char)(55 * light);
            } else {
                px[0] = (unsigned char)(25 * light);
                px[1] = (unsigned char)(80 * light);
                px[2] = (unsigned char)(170 * light);
            }

            /* Grid lines (every 30 degrees) */
            double lat_mod = fmod(fabs(lat), 30.0);
            double lon_mod = fmod(fabs(lon), 30.0);
            if (lat_mod < 1.8 || lat_mod > 28.2 || lon_mod < 1.8 || lon_mod > 28.2) {
                px[0] = (unsigned char)(px[0] * 0.7);
                px[1] = (unsigned char)(px[1] * 0.7);
                px[2] = (unsigned char)(px[2] * 0.7);
            }

            px[3] = 255;
        }
    }
}

#endif