/* nightmesh.h — Day/night overlay mesh generation.
 *
 * Generates a polar triangle mesh (180 angular × 60 radial divisions)
 * covering the Earth disc.  Each vertex carries a per-vertex alpha derived
 * from the solar zenith angle (transparent in daylight, opaque at night,
 * smooth gradient through twilight).  Rebuilt every 60 seconds. */

#ifndef NIGHTMESH_H
#define NIGHTMESH_H

#include "solar.h"

typedef struct {
    float *vertices;      /* interleaved x, y, alpha (3 floats per vertex) */
    int    vertex_count;
    int    capacity;
} NightMesh;

void nightmesh_init(NightMesh *nm);
void nightmesh_build(NightMesh *nm, const SubsolarPoint *sun);
void nightmesh_free(NightMesh *nm);

#endif
