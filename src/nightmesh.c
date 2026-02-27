#include <math.h>
#include <stdlib.h>
#include <string.h>
#include "nightmesh.h"
#include "projection.h"

#define ANGULAR_DIVS  180
#define RADIAL_DIVS    60
#define MAX_ALPHA      0.75f

/* Smooth alpha from zenith angle: 0 at zenith<=80, MAX_ALPHA at zenith>=108 */
static float zenith_to_alpha(double z)
{
    if (z <= 80.0) return 0.0f;
    if (z >= 108.0) return MAX_ALPHA;
    /* Smooth interpolation over the 80-108 degree range */
    double t = (z - 80.0) / 28.0;
    /* Smoothstep for nice easing */
    t = t * t * (3.0 - 2.0 * t);
    return (float)(t * MAX_ALPHA);
}

void nightmesh_init(NightMesh *nm)
{
    /* Inner ring: ANGULAR_DIVS triangles (3 verts each)
     * Outer rings: ANGULAR_DIVS * (RADIAL_DIVS-1) quads * 2 tris * 3 verts */
    int max_verts = ANGULAR_DIVS * 3 + ANGULAR_DIVS * (RADIAL_DIVS - 1) * 6;
    nm->vertices = malloc(max_verts * 3 * sizeof(float));
    nm->capacity = max_verts;
    nm->vertex_count = 0;
}

static void emit_vert(NightMesh *nm, float x, float y, float alpha)
{
    if (nm->vertex_count < nm->capacity) {
        int i = nm->vertex_count * 3;
        nm->vertices[i]     = x;
        nm->vertices[i + 1] = y;
        nm->vertices[i + 2] = alpha;
        nm->vertex_count++;
    }
}

void nightmesh_build(NightMesh *nm, const SubsolarPoint *sun)
{
    double max_r = projection_get_radius() - 0.5; /* inset to avoid float-precision boundary miss */
    float dr = (float)(max_r / RADIAL_DIVS);
    float da = 2.0f * (float)M_PI / ANGULAR_DIVS;

    nm->vertex_count = 0;

    /* Precompute alpha for each grid vertex */
    int rows = RADIAL_DIVS + 1;  /* 0 to RADIAL_DIVS inclusive */
    int cols = ANGULAR_DIVS;
    float *alpha_grid = malloc(rows * cols * sizeof(float));

    for (int ri = 0; ri <= RADIAL_DIVS; ri++) {
        float r = ri * dr;
        for (int ai = 0; ai < ANGULAR_DIVS; ai++) {
            float a = ai * da;
            float x = r * cosf(a);
            float y = r * sinf(a);
            float av = 0.0f;

            if (ri == 0) {
                /* Center point — compute once */
                double lat, lon;
                if (projection_inverse(0.0, 0.0, &lat, &lon) == 0)
                    av = zenith_to_alpha(solar_zenith_angle(lat, lon, sun));
            } else {
                double lat, lon;
                if (projection_inverse((double)x, (double)y, &lat, &lon) == 0)
                    av = zenith_to_alpha(solar_zenith_angle(lat, lon, sun));
                else
                    av = MAX_ALPHA;  /* outside globe = dark */
            }
            alpha_grid[ri * cols + ai] = av;
        }
    }

    /* Generate triangles, skip quads where all vertices have alpha == 0 */
    float center_alpha = alpha_grid[0];  /* same for all ai when ri==0 */

    for (int ai = 0; ai < ANGULAR_DIVS; ai++) {
        int ai_next = (ai + 1) % ANGULAR_DIVS;
        float a0 = ai * da;
        float a1 = (ai + 1) * da;

        /* Inner ring: triangle from center to first ring */
        {
            float a_v0 = center_alpha;
            float a_v1 = alpha_grid[1 * cols + ai];
            float a_v2 = alpha_grid[1 * cols + ai_next];

            if (a_v0 > 0.0f || a_v1 > 0.0f || a_v2 > 0.0f) {
                float r1 = dr;
                emit_vert(nm, 0.0f, 0.0f, a_v0);
                emit_vert(nm, r1 * cosf(a0), r1 * sinf(a0), a_v1);
                emit_vert(nm, r1 * cosf(a1), r1 * sinf(a1), a_v2);
            }
        }

        /* Outer rings */
        for (int ri = 1; ri < RADIAL_DIVS; ri++) {
            float r0 = ri * dr;
            float r1 = (ri + 1) * dr;

            float cos_a0 = cosf(a0), sin_a0 = sinf(a0);
            float cos_a1 = cosf(a1), sin_a1 = sinf(a1);

            float a00 = alpha_grid[ri * cols + ai];
            float a01 = alpha_grid[ri * cols + ai_next];
            float a10 = alpha_grid[(ri + 1) * cols + ai];
            float a11 = alpha_grid[(ri + 1) * cols + ai_next];

            /* Skip fully transparent quads */
            if (a00 == 0.0f && a01 == 0.0f && a10 == 0.0f && a11 == 0.0f)
                continue;

            /* Triangle 1 */
            emit_vert(nm, r0 * cos_a0, r0 * sin_a0, a00);
            emit_vert(nm, r1 * cos_a0, r1 * sin_a0, a10);
            emit_vert(nm, r1 * cos_a1, r1 * sin_a1, a11);
            /* Triangle 2 */
            emit_vert(nm, r0 * cos_a0, r0 * sin_a0, a00);
            emit_vert(nm, r1 * cos_a1, r1 * sin_a1, a11);
            emit_vert(nm, r0 * cos_a1, r0 * sin_a1, a01);
        }
    }

    free(alpha_grid);
}

void nightmesh_free(NightMesh *nm)
{
    free(nm->vertices);
    nm->vertices = NULL;
    nm->vertex_count = 0;
}
