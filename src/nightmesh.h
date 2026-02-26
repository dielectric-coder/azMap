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
