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
  overlay.h/c       MUF contour line + aurora heatmap overlay parsing and mesh building
  fetch.h/c         Threaded non-blocking HTTP fetch (libcurl + pthread)
  cJSON.h/c         Vendored cJSON library (MIT) for JSON parsing
  renderer.h/c      OpenGL shader compilation, VAO/VBO management, draw calls
  camera.h/c        Orthographic view state (zoom, pan), MVP matrix
  input.h/c         GLFW callbacks: scroll, drag, popup drag, keyboard
  ui.h/c            UI system: buttons, draggable popup panel, text input
  text.h/c          Vector stroke font for on-screen text
  qrz.h/c           QRZ.com callsign lookup via XML API (libcurl)
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
| 2 | Land fill (stencil) | Medium gray (0.30, 0.30, 0.30) | GL_TRIANGLE_FAN (stencil) |
| 3 | Earth boundary circle | Dark blue (0.15, 0.15, 0.3) | GL_LINE_LOOP |
| 4 | Grid (rings+radials or parallels+meridians) | Dim (0.2, 0.2, 0.3) | GL_LINE_STRIP |
| 5 | Night overlay | Dark (0.0, 0.0, 0.05) × per-vertex alpha | GL_TRIANGLES |
| 5b | Aurora overlay | Green (0.0, 0.8, 0.2) × per-vertex alpha | GL_TRIANGLES |
| 6 | Country borders | Gray (0.4, 0.4, 0.5) | GL_LINE_STRIP |
| 7 | Coastlines | Dark gray (0.35, 0.35, 0.35) | GL_LINE_STRIP |
| 7b | MUF contour lines | Per-segment color (from KC2G GeoJSON) | GL_LINE_STRIP |
| 8 | Target line (great circle) | Yellow (1.0, 0.9, 0.2) | GL_LINE_STRIP |
| 9 | Center marker | White (1.0, 1.0, 1.0) | GL_TRIANGLE_FAN |
| 10 | Target marker | Red (1.0, 0.3, 0.2) | GL_LINE_LOOP |
| 11 | North pole triangle | White (1.0, 1.0, 1.0) | GL_TRIANGLES |
| 12 | Location labels | Cyan / Orange | GL_LINES (pixel-space) |
| 13 | UI button fills (rounded rects) | Variable | GL_TRIANGLES (pixel-space) |
| 13b | UI button outlines | Variable | GL_LINES (pixel-space) |
| 13c | UI button text | White | GL_LINES (pixel-space) |
| 13d | Section labels + dividers | White | GL_LINES (pixel-space) |
| 13e | MUF legend swatches | Per-entry color | GL_LINES (pixel-space, lineWidth=3) |
| 13f | MUF legend text | Light gray (0.85, 0.85, 0.95) | GL_LINES (pixel-space) |
| 14 | Popup panel | Variable | GL_TRIANGLES + GL_LINES (pixel-space) |
| 15 | HUD text (dist/az) | White (1.0, 1.0, 1.0) | GL_LINES (pixel-space) |

Layers 12-15 use a pixel-space orthographic matrix (y-down) instead of the map MVP.

### Land Fill (Stencil Buffer)

Land polygons from `ne_110m_land` are rendered as filled areas using the stencil buffer inversion technique:

1. **Mark disc**: Draw earth disc into stencil bit 7 (0x80) — restricts subsequent writes to the visible hemisphere
2. **Invert land rings**: Draw each polygon ring as `GL_TRIANGLE_FAN` with `GL_INVERT` on lower stencil bits, only where bit 7 is set
3. **Color pass**: Draw disc with land color where `stencil > 0x80` (disc bit + odd inversions)

This handles concave polygons and holes (e.g. Caspian Sea) via the odd-even fill rule without triangulation. Land polygons use `map_data_reproject_nosplit()` which clips polygon rings to the clipping boundary before projection: edges crossing the boundary are bisected (14-iteration binary search in lat/lon) to find the intersection point, outside vertices are discarded, and boundary crossing points are inserted. The clipped rings are then projected via `projection_forward_clamped()`. Rings entirely outside the boundary are skipped (`segment_clamped` flag). This prevents outside vertices from corrupting the stencil with incorrect fan triangles.

The clipping boundary differs by projection mode. In ORTHO mode, it is the hemisphere boundary (back-hemisphere detection via `projection_forward()` return value). In AZEQ mode, all points project successfully (no back hemisphere), so the boundary is a 175° angular distance threshold from the projection center (using `projection_distance()`). The unified `is_back_vertex()` function abstracts this difference, and `find_boundary_crossing()` uses it for bisection in both modes.

**Boundary arc insertion** (both modes): After projection, shortcut edges between boundary crossing points are detected (both endpoints near the boundary circle and edge length > 0.5×radius). For each shortcut, 12 intermediate arc vertices are inserted along the boundary circle, computed by angle interpolation. The boundary radius is `EARTH_RADIUS_KM` for ORTHO and `clip_max_dist` (175° in km) for AZEQ. This prevents the stencil triangle fan from sweeping across the wrong area when a clipped polygon has multiple boundary crossings.

### Great Circle Target Line

The center-to-target line is rendered as a 101-point `GL_LINE_STRIP` computed via spherical linear interpolation (slerp). Intermediate lat/lon points are projected through `projection_forward_clamped()`. In azeq mode centered on the origin, the points are naturally collinear (straight line). In orthographic mode, the line appears as a curved great circle arc.

### Day/Night Overlay

The day/night system uses two new modules:

- **`solar.c`** computes the subsolar point (latitude/longitude where the sun is directly overhead) from system UTC time using simplified astronomical formulas: solar declination from day-of-year and subsolar longitude from hour angle.
- **`nightmesh.c`** generates a polar mesh (180 angular × 60 radial divisions) covering the Earth disc. For each vertex, `projection_inverse()` converts km-space back to lat/lon, then `solar_zenith_angle()` determines the sun angle. A smoothstep function maps zenith angle to per-vertex alpha: transparent at <=80° (full day), max opacity at >=108° (astronomical night).

The mesh uses 3-component vertices (x, y, alpha). The vertex shader passes the alpha attribute to the fragment shader, which multiplies it with the uniform color alpha. All non-night geometry uses a default vertex alpha of 1.0 set via `glVertexAttrib1f(1, 1.0f)`. The mesh is regenerated every 60 seconds.

### MUF Contour Overlay

The MUF overlay displays Maximum Usable Frequency contour lines from the KC2G propagation service (`prop.kc2g.com`). Implementation in `overlay.c`:

- **Data source**: GeoJSON from `https://prop.kc2g.com/renders/current/mufd-normal-now.geojson` — a FeatureCollection of LineString features, each with a `level-value` (MHz) and `stroke` (hex color) in properties.
- **Parsing** (`muf_parse_geojson()`): Extracts coordinates, stroke colors (hex→RGBA), and level values. Deduplicates legend entries by MHz value and sorts ascending.
- **Projection** (`muf_reproject()`): Forward-projects raw lat/lon through `projection_forward()`, then splits segments at jumps >5000 km (same threshold as `map_data.c`). Each sub-segment inherits the parent segment's color.
- **Storage**: `MufData` stores both raw lat/lon (for reprojection on center/mode change) and projected vertices with per-segment color arrays.
- **Legend**: `MufLegendEntry` array stores unique (MHz, color) pairs. The sidebar renders colored line swatches (GL_LINES, lineWidth=3) with MHz labels, left-aligned above the LAYERS section label.

### Aurora Heatmap Overlay

The aurora overlay displays aurora probability from the NOAA OVATION service. Implementation in `overlay.c`:

- **Data source**: JSON from `https://services.swpc.noaa.gov/json/ovation_aurora_latest.json` — contains a `coordinates` array of `[lon, lat, aurora_probability]` triplets at 1° resolution.
- **Parsing** (`aurora_parse_json()`): Populates an `AuroraGrid` — a 360×181 int array indexed by `[lon * 181 + (lat+90)]`.
- **Mesh building** (`aurora_mesh_build()`): Same polar-grid approach as `nightmesh.c` (180 angular × 60 radial divisions). For each vertex, `projection_inverse()` converts km→lat/lon, then the nearest grid cell is looked up. Probability maps to alpha: 0–5% → transparent, 5–50% → 0.0–0.5, 50–100% → 0.5–0.75. Fully transparent quads are skipped.
- **Rendering**: Drawn as GL_TRIANGLES with uniform green color (0.0, 0.8, 0.2) and per-vertex alpha, after the night overlay and before borders.

### Geomagnetic Indices (Kp/Bz)

When the Aurora layer is active, geomagnetic indices are fetched and displayed in the sidebar (right-aligned, next to the MUF legend area):

- **Kp index**: Fetched from `https://services.swpc.noaa.gov/products/noaa-planetary-k-index.json` — array of arrays, last entry contains the current Kp value (0–9 scale). Parsed by `geomag_parse_kp()`.
- **Bz component**: Fetched from `https://services.swpc.noaa.gov/products/summary/solar-wind-mag-field.json` — JSON object with `"Bz"` string field (nT, negative = southward/geo-effective). Parsed by `geomag_parse_bz()`.
- **Display**: `"Kp X.X"` and `"Bz X.X nT"` rendered right-aligned in the sidebar at 14px font size, same vertical position as MUF legend. Auto-refreshes every 15 minutes with the overlay data.

### Async HTTP Fetch

`fetch.c` provides non-blocking HTTP GET using libcurl in a detached pthread:

- `fetch_start(req, url)` — spawns a detached thread that performs `curl_easy_perform()`. Response body is accumulated in a realloc'd buffer.
- `fetch_check(req)` — non-blocking mutex-protected status poll (0=pending, 1=done, -1=error). Called each frame from the main loop.
- `fetch_take_response(req)` — transfers ownership of the response string to the caller.
- `fetch_cleanup(req)` — frees URL and any remaining response data, destroys mutex.

Both overlays auto-refresh every 15 minutes (`OVERLAY_UPDATE_SEC`) while their toggle is active. The first activation triggers an immediate fetch. Toggling off clears the GPU geometry (sets vertex/segment count to 0).

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

### Main Loop Helpers (`main.c`)

Several static helper functions in `main.c` reduce duplication in the main loop:

- **`str_upper(dst, dst_sz, src)`** — uppercase a string into a destination buffer (null-terminated)
- **`parse_station_detail(ui, detail_str)`** — parse pipe-delimited detail string (`station|freq|country|site|lang|target`) into `ui->station_info[]` with label prefixes (STN, FREQ, CTRY, SITE, LANG, TGT)
- **`reproject_all(map, borders, has_borders, land, has_land, renderer)`** — reproject and re-upload all map geometry (coastlines, borders, land) after a projection center or mode change
- **`update_target_geometry(..., recompute_dist)`** — recompute distance/azimuth (if `recompute_dist`), forward-project center and target, and rebuild the great-circle line. Called from FIFO handler, QRZ success, center-dirty, and projection toggle
- **`clear_target_state(ui, dist, az_to, az_from, renderer, last_text_update)`** — clear station info, zero distance/azimuth, remove target line, hide popup, and force HUD rebuild. Used by QRZ, WSJT, and BCB button handlers

Named constants at the top of `main.c`: `SIDEBAR_WIDTH_PX` (300), `MARKER_ZOOM_FACTOR` (0.005), `BUTTON_HEIGHT` (28), `NIGHT_UPDATE_SEC` (60).

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
| libcurl | HTTP requests (QRZ lookup, MUF/aurora data) | `curl` |
| pthread | Threaded non-blocking HTTP fetches | (glibc) |
| OpenGL 3.3+ | Rendering | (driver) |

### Installing

```bash
sudo cmake --install build
```

Installs the binary to `<prefix>/bin/`, shaders to `<prefix>/share/azmap/shaders/`, and map data (`.shp`, `.shx`, `.dbf`, `.prj` files) to `<prefix>/share/azmap/data/`.

### Build Output

- `build/azmap` - the executable
- `build/shaders/` - copied from `shaders/` on every build

### Path Resolution

The executable resolves data paths relative to its own location using a two-step fallback:

1. **Build tree**: `<exe_dir>/../<rel>` (e.g. `../data/`, `../shaders/`)
2. **Installed layout**: `<exe_dir>/../share/azmap/<rel>` (e.g. `../share/azmap/data/`)

The first path that exists wins. This allows the same binary to work unmodified in both the build directory and after `cmake --install`.
