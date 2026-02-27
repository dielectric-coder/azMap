#ifndef PROJECTION_H
#define PROJECTION_H

#include <math.h>

#define EARTH_RADIUS_KM 6371.0
#define EARTH_MAX_PROJ_RADIUS (M_PI * EARTH_RADIUS_KM)  /* ~20015 km */

typedef enum { PROJ_AZEQ, PROJ_ORTHO } ProjMode;

/* Set/get projection mode (azimuthal equidistant or orthographic). */
void projection_set_mode(ProjMode mode);
ProjMode projection_get_mode(void);

/* Earth radius in projected km-space for the current mode. */
double projection_get_radius(void);

/* Set the center point of the projection (degrees). */
void projection_set_center(double lat_deg, double lon_deg);

/* Get current center (degrees). */
void projection_get_center(double *lat_deg, double *lon_deg);

/* Forward projection: lat/lon (degrees) → x,y (km from center).
 * Returns 0 on success, -1 if the point is antipodal (undefined). */
int projection_forward(double lat_deg, double lon_deg, double *x, double *y);

/* Like projection_forward but clamps ortho back-hemisphere points to the
 * boundary circle instead of returning (1e6,1e6). Always returns 0. */
int projection_forward_clamped(double lat_deg, double lon_deg, double *x, double *y);

/* Inverse projection: x,y (km) → lat/lon (degrees).
 * Returns 0 on success, -1 if the point is outside the globe. */
int projection_inverse(double x, double y, double *lat_deg, double *lon_deg);

/* Great-circle distance between two points in km (degrees input). */
double projection_distance(double lat1, double lon1, double lat2, double lon2);

/* Azimuth from point 1 to point 2 in degrees (0=North, clockwise). */
double projection_azimuth(double lat1, double lon1, double lat2, double lon2);

#endif
