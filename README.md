# azMap

Interactive map projection viewer with two modes: azimuthal equidistant (full Earth) and orthographic (hemisphere sphere view). Given a center location, it projects the world map and draws a line to a target location, showing distance and azimuth information.

![OpenGL](https://img.shields.io/badge/OpenGL-3.3%2B-blue)
![C11](https://img.shields.io/badge/C-11-green)
![License](https://img.shields.io/badge/license-MIT-lightgrey)

## Features

- Two projection modes toggled via "Proj" button:
  - **Azimuthal equidistant** — full Earth, range rings + radial lines grid
  - **Orthographic** — hemisphere sphere view, geographic parallels + meridians grid
- Filled land masses (stencil-buffer polygon fill) with coastline and country border outlines
- Great-circle line between center and target locations (curved arc in orthographic mode)
- Distance, azimuth-to, and azimuth-from readout with live local/UTC clocks
- Named location labels (optional `-c` / `-t` flags)
- North pole indicator triangle
- Real-time day/night overlay with smooth twilight gradient (civil, nautical, astronomical)
- Sidebar panel with UTC/local clocks, station info, distance/azimuth readouts
- Rounded rectangle buttons with hover highlighting, organized in labeled sections (LAYERS / SOURCE)
- **MUF contour overlay** — live Maximum Usable Frequency contour lines from KC2G (prop.kc2g.com), colored by HF band, with sidebar legend
- **Aurora overlay** — live NOAA OVATION aurora probability heatmap (green, per-vertex alpha), with Kp/Bz geomagnetic indices in sidebar
- QRZ callsign lookup via popup with results displayed in sidebar
- FIFO IPC for live target updates from swl dashboard
- Non-blocking HTTP fetches (libcurl + pthread) with 15-minute auto-refresh for live overlays
- Smooth zoom (10 km to full Earth) and pan
- Vector stroke font for all text (no external font dependencies)

## Quick Start

```bash
# Install dependencies (Arch/Manjaro)
sudo pacman -S glfw shapelib glew curl

# Build
mkdir -p build && cd build && cmake .. && make

# Install to ~/.local (default prefix)
cmake --install .


# Run: center on Madrid, line to Paris
./azmap 40.4168 -3.7038 48.8566 2.3522 -c Madrid -t Paris

# Or set up a config file for your QTH and just pass the target:
echo -e "name = Madrid\nlat = 40.4168\nlon = -3.7038" > ~/.config/azmap.conf
./azmap 48.8566 2.3522 -t Paris
```

See [User Guide](doc/user-guide.md) for full usage details and [Developer Guide](doc/dev-guide.md) for architecture and contributing info.

## Map Data

Download [Natural Earth 110m](https://www.naturalearthdata.com/downloads/) shapefiles and extract into `data/`:

| Layer | Required | Source |
|-------|----------|--------|
| `ne_110m_coastline` | Yes | [110m physical vectors](https://www.naturalearthdata.com/downloads/110m-physical-vectors/) |
| `ne_110m_land` | No | [110m physical vectors](https://www.naturalearthdata.com/downloads/110m-physical-vectors/) |
| `ne_110m_admin_0_boundary_lines_land` | No | [110m cultural vectors](https://www.naturalearthdata.com/downloads/110m-cultural-vectors/) |

## Controls

| Input | Action |
|-------|--------|
| Scroll wheel | Zoom in/out |
| Left-drag / Arrow keys | Pan |
| Proj button | Toggle azimuthal equidistant / orthographic projection |
| QRZ button | Callsign lookup via popup, results in sidebar |
| WSJT button | WSJT-X integration (placeholder) |
| BCB button | Clear station info, target line, and distance/azimuth |
| Aurora button | Toggle live aurora probability heatmap overlay |
| MUF button | Toggle live MUF contour lines overlay with sidebar legend |
| R | Reset view |
| Q / Esc | Quit |

## Planned Features

This list is tentative and will likely change.

0. ~~Configuration file for QTH (home station location)~~ Done
1. ~~QRZ callsign lookup~~ Done
2. WSJT-X integration
3. ~~MUF contour overlay (KC2G)~~ Done
4. ~~Aurora overlay (NOAA OVATION)~~ Done
5. ~~Kp/Bz geomagnetic indices~~ Done

## License

MIT
