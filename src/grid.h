#ifndef GRID_H
#define GRID_H

#include "map_data.h"

/* Build range-ring / azimuth-line grid for azimuthal equidistant mode.
 * Uses the current projection center. Caller must free with map_data_free(). */
void grid_build(MapData *md);

/* Build geographic graticule (parallels + meridians) for orthographic mode.
 * Uses projection_forward() so grid depends on current center. */
void grid_build_geo(MapData *md);

#endif
