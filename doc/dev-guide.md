# azMap Developer Guide

## Architecture

azMap is a single-binary C11 application using OpenGL 3.3 core profile. All rendering uses a single shader program with a uniform color per draw call.

### Source Layout

```
src/
  main.c            Entry point, GLFW window, CLI parsing, main loop
  config.h/c        Config file parser (~/.config/azmap.conf)
  projection.h/c    Map projection math (azimuthal equidistant + orthographic modes)
  map_data.h/c      Shapefile loading (shapelib), vertex arrays, reprojection
  grid.h/c          Grid generation (range rings/radials for azeq; parallels/meridians for ortho)
  solar.h/c         Subsolar point calculation from UTC time
  nightmesh.h/c     Day/night overlay mesh generation (per-vertex alpha)
  renderer.h/c      OpenGL shader compilation, VAO/VBO management, draw calls
  camera.h/c        Orthographic view state (zoom, pan), MVP matrix
  input.h/c         GLFW callbacks: scroll, drag, popup drag, keyboard
  ui.h/c            UI system: buttons, draggable popup panel, text input
  text.h/c          Vector stroke font for on-screen text
shaders/
  map.vert          Vertex shader (MVP * position, per-vertex alpha passthrough)
  map.frag          Fragment shader (uniform color * vertex alpha)
```

### Coordinate System

Both projections map lat/lon to x,y in **kilometers** from the center point. In azimuthal equidistant mode the Earth disc extends to ~20015 km radius; in orthographic mode it extends to 6371 km (one hemisphere). The camera produces an orthographic matrix mapping this km-space to OpenGL clip space. `zoom_km` controls the visible diameter (10 to 40030 km, clamped to 2×radius on projection toggle).

### Data Flow

```
Shapefiles (.shp)
  -> map_data_load(): read raw lat/lon, project via projection_forward()
  -> MapData struct: float *vertices (x,y pairs in km), segment start/count arrays
  -> renderer_upload_*(): upload to GPU as VBOs
  -> renderer_draw(): draw as GL_LINE_STRIP per segment
```

Grid data follows the same `MapData` pattern but is generated procedurally in `grid.c` rather than loaded from a file.

### Rendering Layers

Drawn back to front in `renderer_draw()`:

| Order | Layer | Color | Draw Mode |
|-------|-------|-------|-----------|
| 1 | Earth filled disc (ocean) | Dark blue-gray (0.12, 0.12, 0.25) | GL_TRIANGLE_FAN |
| 2 | Land fill (stencil) | Dark green-gray (0.12, 0.15, 0.10) | GL_TRIANGLE_FAN (stencil) |
| 3 | Earth boundary circle | Dark blue (0.15, 0.15, 0.3) | GL_LINE_LOOP |
| 4 | Grid (rings+radials or parallels+meridians) | Dim (0.2, 0.2, 0.3) | GL_LINE_STRIP |
| 5 | Night overlay | Dark (0.0, 0.0, 0.05) × per-vertex alpha | GL_TRIANGLES |
| 6 | Country borders | Gray (0.4, 0.4, 0.5) | GL_LINE_STRIP |
| 7 | Coastlines | Green (0.2, 0.8, 0.3) | GL_LINE_STRIP |
| 8 | Target line (great circle) | Yellow (1.0, 0.9, 0.2) | GL_LINE_STRIP |
| 9 | Center marker | White (1.0, 1.0, 1.0) | GL_TRIANGLE_FAN |
| 10 | Target marker | Red (1.0, 0.3, 0.2) | GL_LINE_LOOP |
| 11 | North pole triangle | White (1.0, 1.0, 1.0) | GL_TRIANGLES |
| 12 | Location labels | Cyan / Orange | GL_LINES (pixel-space) |
| 13 | UI buttons | Variable | GL_TRIANGLES + GL_LINES (pixel-space) |
| 14 | Popup panel | Variable | GL_TRIANGLES + GL_LINES (pixel-space) |
| 15 | HUD text (dist/az) | White (1.0, 1.0, 1.0) | GL_LINES (pixel-space) |

Layers 12-15 use a pixel-space orthographic matrix (y-down) instead of the map MVP.

### Land Fill (Stencil Buffer)

Land polygons from `ne_110m_land` are rendered as filled areas using the stencil buffer inversion technique:

1. **Mark disc**: Draw earth disc into stencil bit 7 (0x80) — restricts subsequent writes to the visible hemisphere
2. **Invert land rings**: Draw each polygon ring as `GL_TRIANGLE_FAN` with `GL_INVERT` on lower stencil bits, only where bit 7 is set
3. **Color pass**: Draw disc with land color where `stencil > 0x80` (disc bit + odd inversions)

This handles concave polygons and holes (e.g. Caspian Sea) via the odd-even fill rule without triangulation. Land polygons use `map_data_reproject_nosplit()` which clips polygon rings to the front hemisphere before projection: edges crossing the boundary are bisected (12-iteration binary search in lat/lon) to find the intersection point, back-hemisphere vertices are discarded, and boundary crossing points are inserted. The clipped rings are then projected via `projection_forward_clamped()`. Rings entirely in the back hemisphere are skipped (`segment_clamped` flag). This prevents back-hemisphere vertices from corrupting the stencil with incorrect fan triangles.

### Great Circle Target Line

The center-to-target line is rendered as a 101-point `GL_LINE_STRIP` computed via spherical linear interpolation (slerp). Intermediate lat/lon points are projected through `projection_forward_clamped()`. In azeq mode centered on the origin, the points are naturally collinear (straight line). In orthographic mode, the line appears as a curved great circle arc.

### Day/Night Overlay

The day/night system uses two new modules:

- **`solar.c`** computes the subsolar point (latitude/longitude where the sun is directly overhead) from system UTC time using simplified astronomical formulas: solar declination from day-of-year and subsolar longitude from hour angle.
- **`nightmesh.c`** generates a polar mesh (180 angular × 60 radial divisions) covering the Earth disc. For each vertex, `projection_inverse()` converts km-space back to lat/lon, then `solar_zenith_angle()` determines the sun angle. A smoothstep function maps zenith angle to per-vertex alpha: transparent at <=80° (full day), max opacity at >=108° (astronomical night).

The mesh uses 3-component vertices (x, y, alpha). The vertex shader passes the alpha attribute to the fragment shader, which multiplies it with the uniform color alpha. All non-night geometry uses a default vertex alpha of 1.0 set via `glVertexAttrib1f(1, 1.0f)`. The mesh is regenerated every 60 seconds.

### Key Data Structures

**`MapData`** (`map_data.h`) - shared by coastlines, borders, land, and grid:
```c
typedef struct {
    float *vertices;                     // x,y pairs in km
    int    vertex_count;
    int    segment_starts[MAX_SEGMENTS]; // start index per polyline
    int    segment_counts[MAX_SEGMENTS]; // vertex count per polyline
    int    segment_clamped[MAX_SEGMENTS]; // 1 if ring was clipped/skipped (land only)
    int    num_segments;
} MapData;
```

**`Renderer`** (`renderer.h`) - owns all GPU resources (VAOs, VBOs) and the shader program. Each visual layer has its own VAO/VBO pair.

**`Camera`** (`camera.h`) - orthographic view state: `zoom_km`, `pan_x`, `pan_y`, `aspect`.

### Grid System

The grid (`grid.c`) provides two generation modes matching the projection:

- **`grid_build()`** (azimuthal equidistant) — generates geometry directly in km-space:
  - Range rings: concentric circles every 5000 km, from 5000 km to ~20000 km
  - Radial lines: 12 lines from the origin to the Earth boundary at 30-degree intervals

- **`grid_build_geo()`** (orthographic) — generates geographic parallels and meridians by sampling lat/lon points through `projection_forward()`:
  - Parallels: every 30° from -60° to 60°, sampled at 5° longitude steps
  - Meridians: every 30°, sampled at 5° latitude steps
  - Points on the back hemisphere (where `projection_forward()` returns -1) start new segments, producing natural horizon clipping

### Text System

`text.c` implements a vector stroke font where each character is defined as line segments in a normalized 0..1 cell. `text_build()` produces GL_LINES vertices at a given pixel position and size. Characters are rendered using the same shader with a pixel-space orthographic matrix.

Supported characters: A-Z, 0-9, and `.,:/-^` (where `^` renders as a degree symbol).

### Labels

Location labels are rebuilt each frame in the main loop:

1. Transform marker km-positions through the MVP to get screen pixel coordinates
2. Build label text at those pixel positions using `text_build()`
3. Upload combined vertex buffer with `renderer_upload_labels()`
4. The `label_split` field tracks where center label vertices end and target label vertices begin, allowing different colors per label

### Adding a New Rendering Layer

1. Add VAO/VBO fields to the `Renderer` struct in `renderer.h`
2. Add an upload function (pattern: gen VAO/VBO if needed, bind, buffer data, set vertex attrib)
3. Add the draw call in `renderer_draw()` at the appropriate z-order position
4. Add cleanup in `renderer_destroy()`

### Projection Module

`projection.c` implements two projection modes selected via `ProjMode` enum:

- **`PROJ_AZEQ`** (default) — azimuthal equidistant. The entire Earth maps to a disc of radius `EARTH_MAX_PROJ_RADIUS` (~20015 km). All points are always visible.
- **`PROJ_ORTHO`** — orthographic (sphere). One hemisphere maps to a disc of radius `EARTH_RADIUS_KM` (6371 km). Back-hemisphere points (`cos_c <= 0`) are clipped: `projection_forward()` returns -1 and sets coords to 1e6.

API:

- `projection_set_mode(mode)` / `projection_get_mode()` — switch between modes
- `projection_get_radius()` — returns `EARTH_MAX_PROJ_RADIUS` for azeq, `EARTH_RADIUS_KM` for ortho
- `projection_set_center(lat, lon)` — sets the projection center (stored as module-level state)
- `projection_forward(lat, lon, &x, &y)` — lat/lon degrees to km-space (returns -1 if clipped in ortho, coords set to 1e6)
- `projection_forward_clamped(lat, lon, &x, &y)` — like `projection_forward` but clamps ortho back-hemisphere points to the boundary circle instead of 1e6 (always returns 0). Used by land polygon fill and great circle target line.
- `projection_inverse(x, y, &lat, &lon)` — km-space back to lat/lon (uses `asin(rho/R)` for ortho, `rho/R` for azeq)
- `projection_distance(lat1, lon1, lat2, lon2)` — great-circle distance in km
- `projection_azimuth(lat1, lon1, lat2, lon2)` — azimuth in degrees (0=N, clockwise)

### Antipodal / Back-Hemisphere Handling

In azimuthal equidistant mode, points near the antipode produce large jumps in km-space. In orthographic mode, back-hemisphere points are set to 1e6 km. In both cases, `map_data.c` splits polyline segments where consecutive projected points are more than 5000 km apart, preventing visual artifacts. Land polygons use a separate path: `map_data_reproject_nosplit()` clips each polygon ring to the front hemisphere by bisecting boundary-crossing edges (finding the lat/lon where `projection_forward()` transitions from front to back), discarding back-hemisphere vertices, and inserting intersection points. The clipped rings are then projected via `projection_forward_clamped()`. This preserves ring topology for stencil fill while preventing back-hemisphere vertices from creating incorrect stencil inversions.

## Building

```bash
mkdir -p build && cd build
cmake ..
make
```

### Dependencies

| Library | Purpose | Package (Arch) |
|---------|---------|----------------|
| GLFW 3 | Window, input, GL context | `glfw` |
| GLEW | OpenGL extension loading | `glew` |
| shapelib | Shapefile parsing | `shapelib` |
| OpenGL 3.3+ | Rendering | (driver) |

### Build Output

- `build/azmap` - the executable
- `build/shaders/` - copied from `shaders/` by CMake at configure time

The executable resolves data paths relative to its own location (`../data/`, `../shaders/`), so it works both from the build directory and when installed.
