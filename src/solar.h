#ifndef SOLAR_H
#define SOLAR_H

#include <time.h>

typedef struct {
    double lat_deg;   /* subsolar latitude (solar declination) */
    double lon_deg;   /* subsolar longitude */
} SubsolarPoint;

/* Compute the subsolar point for a given UTC time. */
SubsolarPoint solar_subsolar_point(time_t utc_time);

/* Solar zenith angle (degrees) at a given lat/lon.
 * Returns > 90 for nighttime. */
double solar_zenith_angle(double lat_deg, double lon_deg,
                          const SubsolarPoint *sun);

#endif
