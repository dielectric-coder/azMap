# azMap

Interactive azimuthal equidistant map projection viewer. Given a center location, it projects the entire world map centered on that point and draws a line to a target location, showing distance and azimuth information.

![OpenGL](https://img.shields.io/badge/OpenGL-3.3%2B-blue)
![C11](https://img.shields.io/badge/C-11-green)
![License](https://img.shields.io/badge/license-MIT-lightgrey)

## Features

- Azimuthal equidistant projection centered on any point on Earth
- Coastline and country border rendering from Natural Earth shapefiles
- Great-circle line between center and target locations
- Distance, azimuth-to, and azimuth-from readout
- Center-based range/azimuth grid (concentric rings every 5000 km, radial lines every 30 degrees)
- Named location labels (optional `-c` / `-t` flags)
- North pole indicator triangle
- Smooth zoom (10 km to full Earth) and pan
- Vector stroke font for all text (no external font dependencies)

## Quick Start

```bash
# Install dependencies (Arch/Manjaro)
sudo pacman -S glfw shapelib glew

# Build
mkdir -p build && cd build && cmake .. && make

# Run: center on Madrid, line to Paris
./azmap 40.4168 -3.7038 48.8566 2.3522 -c Madrid -t Paris
```

See [User Guide](doc/user-guide.md) for full usage details and [Developer Guide](doc/dev-guide.md) for architecture and contributing info.

## Map Data

Download [Natural Earth 110m](https://www.naturalearthdata.com/downloads/) shapefiles and extract into `data/`:

| Layer | Required | Source |
|-------|----------|--------|
| `ne_110m_coastline` | Yes | [110m physical vectors](https://www.naturalearthdata.com/downloads/110m-physical-vectors/) |
| `ne_110m_admin_0_boundary_lines_land` | No | [110m cultural vectors](https://www.naturalearthdata.com/downloads/110m-cultural-vectors/) |

## Controls

| Input | Action |
|-------|--------|
| Scroll wheel | Zoom in/out |
| Left-drag / Arrow keys | Pan |
| R | Reset view |
| Q / Esc | Quit |

## License

MIT
