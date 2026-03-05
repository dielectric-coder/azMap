#ifndef GRID_H
#define GRID_H

#include "map_data.h"

/* Build range-ring / azimuth-line grid for azimuthal equidistant mode.
 * Uses the current projection center. Caller must free with map_data_free(). */
void grid_build(MapData *md);

/* Build geographic graticule (parallels + meridians) for orthographic mode.
 * Uses projection_forward() so grid depends on current center. */
void grid_build_geo(MapData *md);

/* Build distance circles at fixed intervals from center_lat/lon.
 * Works in both projection modes. Caller must free with map_data_free(). */
void grid_build_dist_circles(MapData *md, double center_lat, double center_lon);

/* Number of distance circles and the interval in km. */
#define DIST_CIRCLE_STEP_KM  2000.0
#define DIST_CIRCLE_COUNT    10  /* 2000, 4000, ..., 20000 */

#endif
