#ifndef GRID_H
#define GRID_H

#include "map_data.h"

/* Build a 30-degree lat/lon graticule into md.
 * Uses the current projection center. Caller must free with map_data_free(). */
void grid_build(MapData *md);

#endif
