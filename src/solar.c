#include <math.h>
#include <time.h>
#include "solar.h"

#define DEG2RAD (M_PI / 180.0)
#define RAD2DEG (180.0 / M_PI)

SubsolarPoint solar_subsolar_point(time_t utc_time)
{
    struct tm gm_buf;
    struct tm *gm = gmtime_r(&utc_time, &gm_buf);
    if (!gm) {
        SubsolarPoint sp = { 0.0, 0.0 };
        return sp;
    }

    /* Day of year (0-based) + fractional day */
    int doy = gm->tm_yday;
    double frac = (gm->tm_hour + gm->tm_min / 60.0 + gm->tm_sec / 3600.0) / 24.0;
    double day = doy + frac;

    /* Solar declination (simplified) */
    double decl = -23.44 * cos(2.0 * M_PI * (day + 10.0) / 365.25);

    /* Subsolar longitude: sun is over lon 0 at 12:00 UTC, moves 15 deg/hr */
    double hours = gm->tm_hour + gm->tm_min / 60.0 + gm->tm_sec / 3600.0;
    double lon = -(hours - 12.0) * 15.0;

    /* Normalize to [-180, 180] */
    while (lon > 180.0)  lon -= 360.0;
    while (lon < -180.0) lon += 360.0;

    SubsolarPoint sp = { decl, lon };
    return sp;
}

double solar_zenith_angle(double lat_deg, double lon_deg,
                          const SubsolarPoint *sun)
{
    double lat1 = lat_deg * DEG2RAD;
    double lon1 = lon_deg * DEG2RAD;
    double lat2 = sun->lat_deg * DEG2RAD;
    double lon2 = sun->lon_deg * DEG2RAD;

    double cos_z = sin(lat1) * sin(lat2) +
                   cos(lat1) * cos(lat2) * cos(lon1 - lon2);

    /* Clamp for numerical safety */
    if (cos_z > 1.0)  cos_z = 1.0;
    if (cos_z < -1.0) cos_z = -1.0;

    return acos(cos_z) * RAD2DEG;
}
