#include <math.h>
#include "projection.h"

#define DEG2RAD (M_PI / 180.0)
#define RAD2DEG (180.0 / M_PI)

static ProjMode proj_mode = PROJ_AZEQ;

static double center_lat_rad;
static double center_lon_rad;
static double center_lat_deg_store;
static double center_lon_deg_store;
static double sin_clat, cos_clat;

void projection_set_mode(ProjMode mode) { proj_mode = mode; }
ProjMode projection_get_mode(void) { return proj_mode; }

double projection_get_radius(void)
{
    return (proj_mode == PROJ_ORTHO) ? EARTH_RADIUS_KM : EARTH_MAX_PROJ_RADIUS;
}

void projection_set_center(double lat_deg, double lon_deg)
{
    center_lat_deg_store = lat_deg;
    center_lon_deg_store = lon_deg;
    center_lat_rad = lat_deg * DEG2RAD;
    center_lon_rad = lon_deg * DEG2RAD;
    sin_clat = sin(center_lat_rad);
    cos_clat = cos(center_lat_rad);
}

void projection_get_center(double *lat_deg, double *lon_deg)
{
    *lat_deg = center_lat_deg_store;
    *lon_deg = center_lon_deg_store;
}

int projection_forward(double lat_deg, double lon_deg, double *x, double *y)
{
    double lat = lat_deg * DEG2RAD;
    double lon = lon_deg * DEG2RAD;
    double dlon = lon - center_lon_rad;

    double sin_lat = sin(lat);
    double cos_lat = cos(lat);
    double cos_dlon = cos(dlon);

    double cos_c = sin_clat * sin_lat + cos_clat * cos_lat * cos_dlon;

    /* Clamp for numerical safety */
    if (cos_c > 1.0) cos_c = 1.0;
    if (cos_c < -1.0) cos_c = -1.0;

    if (proj_mode == PROJ_ORTHO) {
        if (cos_c <= 0.0) {
            /* Back hemisphere — clip */
            *x = 1e6;
            *y = 1e6;
            return -1;
        }
        *x = EARTH_RADIUS_KM * cos_lat * sin(dlon);
        *y = EARTH_RADIUS_KM * (cos_clat * sin_lat - sin_clat * cos_lat * cos_dlon);
        return 0;
    }

    /* Azimuthal equidistant */
    double c = acos(cos_c);

    if (c < 1e-10) {
        *x = 0.0;
        *y = 0.0;
        return 0;
    }

    double k = (c / sin(c)) * EARTH_RADIUS_KM;

    *x = k * cos_lat * sin(dlon);
    *y = k * (cos_clat * sin_lat - sin_clat * cos_lat * cos_dlon);

    return 0;
}

int projection_inverse(double x, double y, double *lat_deg, double *lon_deg)
{
    double rho = sqrt(x * x + y * y);

    if (rho < 1e-10) {
        *lat_deg = center_lat_deg_store;
        *lon_deg = center_lon_deg_store;
        return 0;
    }

    double c, sin_c, cos_c;

    if (proj_mode == PROJ_ORTHO) {
        if (rho > EARTH_RADIUS_KM) return -1;
        c = asin(rho / EARTH_RADIUS_KM);
        sin_c = sin(c);
        cos_c = cos(c);
    } else {
        c = rho / EARTH_RADIUS_KM;
        if (c > M_PI) return -1;
        sin_c = sin(c);
        cos_c = cos(c);
    }

    double lat = asin(cos_c * sin_clat + (y * sin_c * cos_clat) / rho);
    double lon;

    if (fabs(cos_clat) < 1e-10) {
        lon = center_lon_rad + atan2(x, (center_lat_rad > 0 ? -y : y));
    } else {
        lon = center_lon_rad + atan2(x * sin_c,
                                      rho * cos_clat * cos_c - y * sin_clat * sin_c);
    }

    *lat_deg = lat * RAD2DEG;
    *lon_deg = lon * RAD2DEG;
    return 0;
}

double projection_distance(double lat1, double lon1, double lat2, double lon2)
{
    double dlat = (lat2 - lat1) * DEG2RAD;
    double dlon = (lon2 - lon1) * DEG2RAD;
    double a = sin(dlat / 2);
    double b = sin(dlon / 2);
    a = a * a + cos(lat1 * DEG2RAD) * cos(lat2 * DEG2RAD) * b * b;
    return 2.0 * EARTH_RADIUS_KM * asin(sqrt(a));
}

double projection_azimuth(double lat1, double lon1, double lat2, double lon2)
{
    double phi1 = lat1 * DEG2RAD;
    double phi2 = lat2 * DEG2RAD;
    double dlon = (lon2 - lon1) * DEG2RAD;

    double y = sin(dlon) * cos(phi2);
    double x = cos(phi1) * sin(phi2) - sin(phi1) * cos(phi2) * cos(dlon);
    double az = atan2(y, x) * RAD2DEG;

    return fmod(az + 360.0, 360.0);
}
