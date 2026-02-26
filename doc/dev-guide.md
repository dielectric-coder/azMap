# azMap Developer Guide

## Architecture

azMap is a single-binary C11 application using OpenGL 3.3 core profile. All rendering uses a single shader program with a uniform color per draw call.

### Source Layout

```
src/
  main.c            Entry point, GLFW window, CLI parsing, main loop
  projection.h/c    Azimuthal equidistant forward/inverse projection math
  map_data.h/c      Shapefile loading (shapelib), vertex arrays, reprojection
  grid.h/c          Center-based range/azimuth grid generation
  renderer.h/c      OpenGL shader compilation, VAO/VBO management, draw calls
  camera.h/c        Orthographic view state (zoom, pan), MVP matrix
  input.h/c         GLFW callbacks: scroll, drag, keyboard
  text.h/c          Vector stroke font for on-screen text
shaders/
  map.vert          Vertex shader (MVP * position)
  map.frag          Fragment shader (uniform color)
```

### Coordinate System

The azimuthal equidistant projection maps lat/lon to x,y in **kilometers** from the center point. The camera produces an orthographic matrix mapping this km-space to OpenGL clip space. `zoom_km` controls the visible diameter (10 to 40030 km).

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
| 1 | Earth boundary circle | Dark blue (0.15, 0.15, 0.3) | GL_LINE_LOOP |
| 2 | Grid (range rings + radials) | Dim (0.2, 0.2, 0.3) | GL_LINE_STRIP |
| 3 | Country borders | Gray (0.4, 0.4, 0.5) | GL_LINE_STRIP |
| 4 | Coastlines | Green (0.2, 0.8, 0.3) | GL_LINE_STRIP |
| 5 | Target line | Yellow (1.0, 0.9, 0.2) | GL_LINES |
| 6 | Center marker | White (1.0, 1.0, 1.0) | GL_TRIANGLE_FAN |
| 7 | Target marker | Red (1.0, 0.3, 0.2) | GL_LINE_LOOP |
| 8 | North pole triangle | White (1.0, 1.0, 1.0) | GL_TRIANGLES |
| 9 | Location labels | Cyan / Orange | GL_LINES (pixel-space) |
| 10 | HUD text (dist/az) | White (1.0, 1.0, 1.0) | GL_LINES (pixel-space) |

Layers 9-10 use a pixel-space orthographic matrix (y-down) instead of the map MVP.

### Key Data Structures

**`MapData`** (`map_data.h`) - shared by coastlines, borders, and grid:
```c
typedef struct {
    float *vertices;                   // x,y pairs in km
    int    vertex_count;
    int    segment_starts[MAX_SEGMENTS]; // start index per polyline
    int    segment_counts[MAX_SEGMENTS]; // vertex count per polyline
    int    num_segments;
} MapData;
```

**`Renderer`** (`renderer.h`) - owns all GPU resources (VAOs, VBOs) and the shader program. Each visual layer has its own VAO/VBO pair.

**`Camera`** (`camera.h`) - orthographic view state: `zoom_km`, `pan_x`, `pan_y`, `aspect`.

### Grid System

The grid (`grid.c`) generates geometry in km-space directly (no projection needed):

- **Range rings**: concentric circles every 5000 km, from 5000 km to ~20000 km
- **Radial lines**: 12 lines from the origin to the Earth boundary at 30-degree intervals

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

`projection.c` implements the azimuthal equidistant projection:

- `projection_set_center(lat, lon)` - sets the projection center (stored as module-level state)
- `projection_forward(lat, lon, &x, &y)` - lat/lon degrees to km-space
- `projection_inverse(x, y, &lat, &lon)` - km-space back to lat/lon
- `projection_distance(lat1, lon1, lat2, lon2)` - great-circle distance in km
- `projection_azimuth(lat1, lon1, lat2, lon2)` - azimuth in degrees (0=N, clockwise)

### Antipodal Handling

Points near the antipode of the projection center produce large jumps in km-space. `map_data.c` splits polyline segments where consecutive projected points are more than 5000 km apart, preventing visual artifacts.

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
