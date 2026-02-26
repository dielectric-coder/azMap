# azMap User Guide

## Installation

### Dependencies

- **GLFW 3** - windowing and input
- **GLEW** - OpenGL extension loading
- **shapelib** - shapefile parsing
- **OpenGL 3.3+** - rendering

#### Arch / Manjaro

```bash
sudo pacman -S glfw shapelib glew
```

#### Ubuntu / Debian

```bash
sudo apt install libglfw3-dev libglew-dev libshp-dev
```

### Building

```bash
mkdir -p build && cd build
cmake ..
make
```

The binary is placed at `build/azmap`. Shaders are copied to `build/shaders/` automatically.

### Map Data

azMap uses [Natural Earth](https://www.naturalearthdata.com/) 110m shapefiles. Download and extract them so the directory structure looks like:

```
data/
  ne_110m_coastline/
    ne_110m_coastline.shp
    ne_110m_coastline.shx
    ne_110m_coastline.dbf
  ne_110m_admin_0_boundary_lines_land/   (optional)
    ne_110m_admin_0_boundary_lines_land.shp
    ...
```

Coastlines are required. Country borders are optional and will be silently skipped if not found.

## Config File

You can set a default center location (your QTH) in `~/.config/azmap.conf` so you only need to specify the target on the command line:

```
# azMap configuration
name = Madrid
lat = 40.4168
lon = -3.7038
```

- Lines starting with `#` are comments
- Whitespace around `=` is ignored
- `lat` and `lon` must both be present to be used; `name` is optional
- CLI arguments always override config values

## Usage

```
./azmap <center_lat> <center_lon> <target_lat> <target_lon> [options]
./azmap <target_lat> <target_lon> [options]   # center from config
```

### Positional Arguments

| Argument | Description |
|----------|-------------|
| `center_lat` | Latitude of projection center (degrees, -90 to 90) |
| `center_lon` | Longitude of projection center (degrees, -180 to 180) |
| `target_lat` | Latitude of target location |
| `target_lon` | Longitude of target location |

When a valid config file provides the center location, only `target_lat` and `target_lon` are required.

### Options

| Flag | Description |
|------|-------------|
| `-c NAME` | Display name for the center location (overrides config name) |
| `-t NAME` | Display name for the target location |
| `-s PATH` | Override the default coastline shapefile path |

For backward compatibility, a bare fifth positional argument is also accepted as the shapefile path.

### Examples

```bash
# Madrid to Paris, with names
./azmap 40.4168 -3.7038 48.8566 2.3522 -c Madrid -t Paris

# With config file providing center — just pass the target
./azmap 48.8566 2.3522 -t Paris

# Tokyo to New York
./azmap 35.6762 139.6503 40.7128 -74.0060 -c Tokyo -t "New York"

# Custom shapefile
./azmap 51.5074 -0.1278 -33.8688 151.2093 -s /path/to/my.shp
```

## On-Screen Display

### Map Elements

- **Dark blue circle** - Earth boundary (antipodal edge)
- **Dim grid** - Range rings every 5000 km and radial azimuth lines every 30 degrees, centered on your location
- **Gray lines** - Country borders (if data available)
- **Green lines** - Coastlines
- **Yellow line** - Great-circle path from center to target
- **White filled circle** - Center location marker
- **Red outline circle** - Target location marker
- **White triangle** - North pole indicator
- **Dark overlay** - Night side of the Earth with smooth twilight gradient (updates every 60 seconds from system UTC time)

### Text Overlays

- **Top-left HUD** (white) - Distance in km, azimuth to target, azimuth from target
- **Center label** (cyan) - Center location name and coordinates
- **Target label** (orange) - Target location name and coordinates

If no `-c` or `-t` name is given, labels show coordinates only (e.g., `40.42N, 3.70W`).

## Controls

| Input | Action |
|-------|--------|
| Scroll wheel | Zoom in/out (range: 10 km to 40030 km diameter) |
| Left mouse drag | Pan the map |
| Arrow keys | Pan the map |
| R | Reset view (full Earth, centered) |
| Q / Esc | Quit |

## Console Output

On startup, azMap prints summary info to the terminal:

```
Center:   40.4168, -3.7038
Target:   48.8566, 2.3522
Distance: 1053.4 km
Az to:    28.7 deg
Az from:  209.5 deg
```
