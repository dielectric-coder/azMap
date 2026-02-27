# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

azMap is a C application that renders an interactive map projection using OpenGL. It supports two projection modes — azimuthal equidistant (full Earth) and orthographic (hemisphere sphere view) — toggled via a UI button. Given a center lat/lon, it projects the world map and draws a line to a target location, showing distance and azimuth information.

## Build

Dependencies: GLFW3, GLEW, shapelib, OpenGL 3.3+

```bash
# Install dependencies (Arch/Manjaro)
sudo pacman -S glfw shapelib glew

# Build
mkdir -p build && cd build && cmake .. && make
```

## Run

```bash
# From the build directory
./azmap <center_lat> <center_lon> <target_lat> <target_lon> [options]
./azmap <target_lat> <target_lon> [options]   # center from config

# Example: center on Madrid, line to Paris
./azmap 40.4168 -3.7038 48.8566 2.3522 -c Madrid -t Paris

# With config file providing center:
./azmap 48.8566 2.3522 -t Paris
```

### Config File

Optional. Place at `~/.config/azmap.conf`:

```
# azMap configuration
name = Madrid
lat = 40.4168
lon = -3.7038
```

Lines starting with `#` are comments. Whitespace around `=` is ignored. `lat` and `lon` must both be present to be used. CLI args always override config values.

### Options

- `-c NAME` — Center location name (displayed as label; overrides config name)
- `-t NAME` — Target location name (displayed as label)
- `-s PATH` — Shapefile path override

For backward compatibility, a bare fifth argument is still accepted as the shapefile path.

### Map data (Natural Earth 110m)

Download and extract into `data/`:
- **Coastlines** (required): `ne_110m_coastline` from [110m physical vectors](https://www.naturalearthdata.com/downloads/110m-physical-vectors/)
- **Country borders** (optional): `ne_110m_admin_0_boundary_lines_land` from [110m cultural vectors](https://www.naturalearthdata.com/downloads/110m-cultural-vectors/)

## Controls

- Scroll: zoom in/out (10 km to full Earth)
- Left-drag / arrow keys: pan
- R: reset view
- Q / Esc: quit

## Architecture

```
src/
├── main.c          Entry point, GLFW window, main loop, CLI arg parsing, label building
├── config.h/c      Config file parser (~/.config/azmap.conf)
├── projection.h/c  Map projection math (azimuthal equidistant + orthographic modes)
├── map_data.h/c    Shapefile loading (shapelib), vertex array management, reprojection
├── grid.h/c        Grid generation (range rings + radials for azeq; parallels + meridians for ortho)
├── solar.h/c       Subsolar point calculation from UTC time
├── nightmesh.h/c   Day/night overlay mesh generation (per-vertex alpha)
├── renderer.h/c    OpenGL shader compilation, VAO/VBO management, draw calls
├── camera.h/c      Orthographic view state (zoom_km, pan offset), MVP matrix
├── input.h/c       GLFW callbacks: scroll→zoom, drag→pan, keyboard shortcuts
└── text.h/c        Vector stroke font for on-screen text (distance/azimuth overlay)
shaders/
├── map.vert        Vertex shader (MVP transform + per-vertex alpha passthrough)
└── map.frag        Fragment shader (uniform color * vertex alpha)
```

**Projection modes**: Two modes selectable via the "Proj" UI button. Azimuthal equidistant (`PROJ_AZEQ`) maps the entire Earth to a disc of radius ~20015 km. Orthographic (`PROJ_ORTHO`) shows one hemisphere as a sphere of radius 6371 km; back-hemisphere points are clipped (return -1, coords set to 1e6 triggering the split threshold). `projection_get_radius()` returns the mode-appropriate Earth radius.

**Coordinate system**: The projection outputs x,y in kilometers from the center point. The camera builds an orthographic matrix mapping km-space to clip space. `zoom_km` controls the visible diameter (10–40030 km).

**Data flow**: Shapefiles are loaded once → raw lat/lon stored in `map_data.c` statics → projected to km via `projection_forward()` → uploaded to GPU as VBOs → drawn as `GL_LINE_STRIP` segments per polyline. Grid geometry is generated procedurally: range rings + radials in km-space for azeq mode (`grid_build()`), or parallels + meridians through `projection_forward()` for ortho mode (`grid_build_geo()`).

**Rendering layers** (drawn back to front): Earth filled disc (dark blue-gray) → Earth boundary circle (dark blue) → grid rings+radials (dim) → night overlay (semi-transparent, smooth gradient) → country borders (gray) → coastlines (green) → target line (yellow) → center marker (white filled circle) → target marker (red outline circle) → north pole triangle (white) → location labels (cyan/orange, pixel-space) → HUD text overlay (white, pixel-space).

**Day/night overlay**: `solar.c` computes the subsolar point from system UTC time. `nightmesh.c` generates a polar mesh (180x60) covering the Earth disc using `projection_get_radius()` for the disc extent; each vertex gets a per-vertex alpha based on solar zenith angle using a smoothstep function (transparent at zenith<=80°, max opacity at zenith>=108°). The mesh is regenerated every 60 seconds (and on projection toggle). The vertex shader passes per-vertex alpha to the fragment shader, which multiplies it with the uniform color alpha. Non-night geometry uses a default vertex alpha of 1.0 via `glVertexAttrib1f(1, 1.0f)`.

**Text rendering**: Uses a built-in vector stroke font (`text.c`) — characters are defined as line segments, rendered with GL_LINES using the same shader. No external font dependencies.

**HUD text**: The top-left overlay shows distance/azimuth info and live local/UTC clocks, rebuilt every second in the main loop.

**Labels**: Location labels are rebuilt each frame by projecting marker km-positions through the MVP to screen coordinates, then rendered in pixel-space. The center label is cyan and the target label is orange.
