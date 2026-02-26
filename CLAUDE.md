# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

azMap is a C application that renders an interactive azimuthal equidistant map projection using OpenGL. Given a center lat/lon, it projects the world map and draws a line to a target location, showing distance and azimuth information.

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
├── projection.h/c  Azimuthal equidistant forward/inverse projection math
├── map_data.h/c    Shapefile loading (shapelib), vertex array management, reprojection
├── grid.h/c        Center-based range/azimuth grid (concentric rings + radial lines)
├── renderer.h/c    OpenGL shader compilation, VAO/VBO management, draw calls
├── camera.h/c      Orthographic view state (zoom_km, pan offset), MVP matrix
├── input.h/c       GLFW callbacks: scroll→zoom, drag→pan, keyboard shortcuts
└── text.h/c        Vector stroke font for on-screen text (distance/azimuth overlay)
shaders/
├── map.vert        Vertex shader (MVP transform on 2D positions)
└── map.frag        Fragment shader (uniform color per draw call)
```

**Coordinate system**: The projection outputs x,y in kilometers from the center point. The camera builds an orthographic matrix mapping km-space to clip space. `zoom_km` controls the visible diameter (10–40030 km).

**Data flow**: Shapefiles are loaded once → raw lat/lon stored in `map_data.c` statics → projected to km via `projection_forward()` → uploaded to GPU as VBOs → drawn as `GL_LINE_STRIP` segments per polyline. Grid geometry is generated procedurally in km-space.

**Rendering layers** (drawn back to front): Earth boundary circle (dark blue) → grid rings+radials (dim) → country borders (gray) → coastlines (green) → target line (yellow) → center marker (white filled circle) → target marker (red outline circle) → north pole triangle (white) → location labels (cyan/orange, pixel-space) → HUD text overlay (white, pixel-space).

**Text rendering**: Uses a built-in vector stroke font (`text.c`) — characters are defined as line segments, rendered with GL_LINES using the same shader. No external font dependencies.

**Labels**: Location labels are rebuilt each frame by projecting marker km-positions through the MVP to screen coordinates, then rendered in pixel-space. The center label is cyan and the target label is orange.
