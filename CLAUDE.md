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

# QRZ.com credentials (optional, for callsign lookup)
qrz_user = YOURCALL
qrz_pass = yourpassword
```

Lines starting with `#` are comments. Whitespace around `=` is ignored. `lat` and `lon` must both be present to be used. `qrz_user` and `qrz_pass` enable QRZ callsign lookup. CLI args always override config values.

### Options

- `-c NAME` — Center location name (displayed as label; overrides config name)
- `-t NAME` — Target location name (displayed as label)
- `-s PATH` — Shapefile path override

For backward compatibility, a bare fifth argument is still accepted as the shapefile path.

### Map data (Natural Earth 110m)

Download and extract into `data/`:
- **Coastlines** (required): `ne_110m_coastline` from [110m physical vectors](https://www.naturalearthdata.com/downloads/110m-physical-vectors/)
- **Land polygons** (optional): `ne_110m_land` from [110m physical vectors](https://www.naturalearthdata.com/downloads/110m-physical-vectors/)
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
├── input.h/c       GLFW callbacks: scroll→zoom, drag→pan, popup drag, keyboard shortcuts
├── ui.h/c          UI system: buttons, draggable popup panel, text input
└── text.h/c        Vector stroke font for on-screen text (distance/azimuth overlay)
shaders/
├── map.vert        Vertex shader (MVP transform + per-vertex alpha passthrough)
└── map.frag        Fragment shader (uniform color * vertex alpha)
```

**Projection modes**: Two modes selectable via the "Proj" UI button. Azimuthal equidistant (`PROJ_AZEQ`) maps the entire Earth to a disc of radius ~20015 km. Orthographic (`PROJ_ORTHO`) shows one hemisphere as a sphere of radius 6371 km; back-hemisphere points are clipped (return -1, coords set to 1e6 triggering the split threshold). `projection_forward_clamped()` is an alternative that clamps ortho back-hemisphere points to the boundary circle instead of 1e6 — used by land polygon fill and the great circle target line. `projection_get_radius()` returns the mode-appropriate Earth radius.

**Coordinate system**: The projection outputs x,y in kilometers from the center point. The camera builds an orthographic matrix mapping km-space to clip space. `zoom_km` controls the visible diameter (10–40030 km).

**Data flow**: Shapefiles are loaded once → raw lat/lon stored in `map_data.c` statics → projected to km via `projection_forward()` → uploaded to GPU as VBOs → drawn as `GL_LINE_STRIP` segments per polyline. Land polygons use `map_data_reproject_nosplit()` which clips polygon rings to the front hemisphere (bisecting edges at boundary crossings) then projects with `projection_forward_clamped()`. Grid geometry is generated procedurally: range rings + radials in km-space for azeq mode (`grid_build()`), or parallels + meridians through `projection_forward()` for ortho mode (`grid_build_geo()`).

**Rendering layers** (drawn back to front): Earth filled disc (ocean, dark blue-gray) → land fill (dark green-gray, stencil buffer) → Earth boundary circle (dark blue) → grid rings+radials (dim) → night overlay (semi-transparent, smooth gradient) → country borders (gray) → coastlines (green) → target line (yellow, great circle arc) → center marker (white filled circle) → target marker (red outline circle) → north pole triangle (white) → location labels (cyan/orange, pixel-space) → HUD text overlay (white, pixel-space).

**Land fill**: Uses stencil buffer inversion technique for non-convex polygon fill. Step 1: mark the earth disc in stencil bit 7. Step 2: draw each land polygon ring as `GL_TRIANGLE_FAN` with `GL_INVERT` on lower stencil bits, restricted to disc area via stencil test. Step 3: draw disc with land color where stencil > 0x80 (disc + odd inversions). This handles concave polygons and holes (Caspian Sea, etc.) via the odd-even rule. In ortho mode, `map_data_reproject_nosplit()` clips polygon rings to the front hemisphere: edges crossing the boundary are bisected to find the intersection lat/lon, back-hemisphere vertices are discarded, and boundary crossing points are inserted. This prevents clamped back-hemisphere vertices from corrupting the stencil. Rings entirely in the back hemisphere are skipped. Per-segment `segment_clamped` flags track which segments were skipped.

**Target line**: The center-to-target line is rendered as a great circle arc (101-point `GL_LINE_STRIP`). Intermediate points are computed via spherical linear interpolation (slerp) and projected through `projection_forward_clamped()`. In azeq mode centered on the origin, the great circle naturally appears as a straight line. In ortho mode, it appears as a curved arc.

**Day/night overlay**: `solar.c` computes the subsolar point from system UTC time. `nightmesh.c` generates a polar mesh (180x60) covering the Earth disc using `projection_get_radius()` for the disc extent (with a small inset to avoid float-precision boundary misses at the limb); each vertex gets a per-vertex alpha based on solar zenith angle using a smoothstep function (transparent at zenith<=80°, max opacity at zenith>=108°). The mesh is regenerated every 60 seconds (and on projection toggle). The vertex shader passes per-vertex alpha to the fragment shader, which multiplies it with the uniform color alpha. Non-night geometry uses a default vertex alpha of 1.0 via `glVertexAttrib1f(1, 1.0f)`.

**Text rendering**: Uses a built-in vector stroke font (`text.c`) — characters are defined as line segments, rendered with GL_LINES using the same shader. No external font dependencies.

**HUD text**: The top-center overlay shows distance/azimuth info and live local/UTC clocks (using reentrant `gmtime_r`/`localtime_r`), rebuilt every second in the main loop.

**Labels**: Location labels are rebuilt each frame by projecting marker km-positions through the MVP to screen coordinates, then rendered in pixel-space. The center label is cyan and the target label is orange.

**UI popup**: The `UIPopup` struct stores position offset (`offset_x`, `offset_y`) for drag support. The popup is centered on show (`ui_show_popup()` resets offsets) and can be dragged by its title bar. `input.c` detects title-bar presses via hit-testing the top 30px of the popup bounds, then accumulates cursor deltas into the offset during drag. The `popup_dragging` flag in `InputState` distinguishes popup drags from map pans. Clicks outside the popup pass through to button hit-testing; clicks inside are consumed.
