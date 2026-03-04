#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <libgen.h>
#include <limits.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <math.h>
#include <time.h>
#include <GL/glew.h>
#include <GLFW/glfw3.h>

#include "projection.h"
#include "map_data.h"
#include "renderer.h"
#include "camera.h"
#include "input.h"
#include "text.h"
#include "grid.h"
#include "config.h"
#include "solar.h"
#include "nightmesh.h"
#include "ui.h"
#include "qrz.h"
#include "overlay.h"
#include "fetch.h"

#define DEFAULT_WIDTH  800
#define DEFAULT_HEIGHT 800
#define SIDEBAR_WIDTH_PX 300.0f
#define MARKER_ZOOM_FACTOR 0.005f
#define BUTTON_HEIGHT 28.0f
#define NIGHT_UPDATE_SEC 60
#define DEFAULT_SHP_REL "data/ne_110m_coastline/ne_110m_coastline.shp"
#define DEFAULT_BORDER_REL "data/ne_110m_admin_0_boundary_lines_land/ne_110m_admin_0_boundary_lines_land.shp"
#define DEFAULT_LAND_REL "data/ne_110m_land/ne_110m_land.shp"
#define DEFAULT_SHADER_REL "shaders"

/* Resolve a path relative to the executable's directory.
 * Tries <exe_dir>/../<rel> first (build tree), then
 * <exe_dir>/../share/azmap/<rel> (installed layout). */
static void resolve_path(const char *exe, const char *rel, char *out, size_t out_size)
{
    char exe_copy[PATH_MAX];
    strncpy(exe_copy, exe, PATH_MAX - 1);
    exe_copy[PATH_MAX - 1] = '\0';
    char *dir = dirname(exe_copy);

    /* Try build-tree layout first */
    snprintf(out, out_size, "%s/../%s", dir, rel);
    if (access(out, F_OK) == 0)
        return;

    /* Fall back to installed layout */
    snprintf(out, out_size, "%s/../share/azmap/%s", dir, rel);
}

/* Format a coordinate as "12.34N" or "12.34S" / "1.23E" or "1.23W" */
static int format_coord(char *buf, size_t sz, double lat, double lon)
{
    char ns = lat >= 0 ? 'N' : 'S';
    char ew = lon >= 0 ? 'E' : 'W';
    return snprintf(buf, sz, "%.2f%c, %.2f%c", fabs(lat), ns, fabs(lon), ew);
}

/* Build a label string: "Name (12.34N, 1.23W)" or just "12.34N, 1.23W" */
static void build_label(char *buf, size_t sz, const char *name, double lat, double lon)
{
    char coord[64];
    format_coord(coord, sizeof(coord), lat, lon);
    if (name && name[0])
        snprintf(buf, sz, "%s (%s)", name, coord);
    else
        snprintf(buf, sz, "%s", coord);
}

/* Transform a km-space point through MVP to get pixel coords.
 * mvp is column-major 4x4. Returns pixel x,y (origin top-left). */
static void km_to_pixel(const float *mvp, float kx, float ky,
                         int fb_w, int fb_h, float *px, float *py)
{
    /* clip = MVP * (kx, ky, 0, 1) */
    float cx = mvp[0]*kx + mvp[4]*ky + mvp[12];
    float cy = mvp[1]*kx + mvp[5]*ky + mvp[13];
    float cw = mvp[3]*kx + mvp[7]*ky + mvp[15];
    /* NDC */
    float nx = cx / cw;
    float ny = cy / cw;
    /* pixel (y-down for text system) */
    *px = (nx * 0.5f + 0.5f) * (float)fb_w;
    *py = (-ny * 0.5f + 0.5f) * (float)fb_h;
}

/* Build a quad (2 triangles, 6 vertices) for a label background.
 * x,y: top-left of text; w,h: text dimensions; pad: padding pixels.
 * Returns 6 (vertices written). */
static int build_label_bg(float x, float y, float w, float h, float pad,
                           float *out)
{
    float x0 = x - pad, y0 = y - pad;
    float x1 = x + w + pad, y1 = y + h + pad;
    /* Triangle 1 */
    out[0]  = x0; out[1]  = y0;
    out[2]  = x1; out[3]  = y0;
    out[4]  = x1; out[5]  = y1;
    /* Triangle 2 */
    out[6]  = x0; out[7]  = y0;
    out[8]  = x1; out[9]  = y1;
    out[10] = x0; out[11] = y1;
    return 6;
}

/* Build great circle path vertices between two lat/lon points.
 * Uses projection_forward_clamped so the path stays on/at the disc boundary.
 * Returns number of vertices written. */
#define GC_LINE_POINTS 101
static int build_gc_line(double lat1, double lon1, double lat2, double lon2,
                          float *verts)
{
    double phi1 = lat1 * M_PI / 180.0, lam1 = lon1 * M_PI / 180.0;
    double phi2 = lat2 * M_PI / 180.0, lam2 = lon2 * M_PI / 180.0;

    double cos_d = sin(phi1)*sin(phi2) + cos(phi1)*cos(phi2)*cos(lam2 - lam1);
    if (cos_d > 1.0) cos_d = 1.0;
    if (cos_d < -1.0) cos_d = -1.0;
    double d = acos(cos_d);

    if (d < 1e-10) {
        double x, y;
        projection_forward_clamped(lat1, lon1, &x, &y);
        verts[0] = (float)x;
        verts[1] = (float)y;
        return 1;
    }

    double sin_d = sin(d);
    int n = GC_LINE_POINTS - 1;

    for (int i = 0; i <= n; i++) {
        double t = (double)i / (double)n;
        double a = sin((1.0 - t) * d) / sin_d;
        double b = sin(t * d) / sin_d;

        double x3 = a * cos(phi1)*cos(lam1) + b * cos(phi2)*cos(lam2);
        double y3 = a * cos(phi1)*sin(lam1) + b * cos(phi2)*sin(lam2);
        double z3 = a * sin(phi1)            + b * sin(phi2);

        double lat = atan2(z3, sqrt(x3*x3 + y3*y3)) * 180.0 / M_PI;
        double lon = atan2(y3, x3) * 180.0 / M_PI;

        double px, py;
        projection_forward_clamped(lat, lon, &px, &py);
        verts[i * 2]     = (float)px;
        verts[i * 2 + 1] = (float)py;
    }

    return n + 1;
}

/* Uppercase a string into dst (always null-terminated). */
static void str_upper(char *dst, size_t dst_sz, const char *src)
{
    size_t i;
    for (i = 0; i < dst_sz - 1 && src[i]; i++)
        dst[i] = (char)toupper((unsigned char)src[i]);
    dst[i] = '\0';
}

/* Clear station info, target line, distance/azimuth, and popup state. */
static void clear_target_state(UI *ui, double *dist, double *az_to, double *az_from,
                               Renderer *renderer, time_t *last_text_update)
{
    ui->station_info_lines = 0;
    *dist = 0; *az_to = 0; *az_from = 0;
    renderer_upload_target_line(renderer, NULL, 0);
    ui_hide_popup(ui);
    ui_popup_clear_input(ui);
    *last_text_update = 0;
}

/* Reproject all map geometry after projection center or mode change. */
static void reproject_all(MapData *map, MapData *borders, int has_borders,
                          MapData *land, int has_land, Renderer *renderer)
{
    map_data_reproject(map);
    renderer_upload_map(renderer, map);
    if (has_borders) {
        map_data_reproject(borders);
        renderer_upload_borders(renderer, borders);
    }
    if (has_land) {
        map_data_reproject_nosplit(land);
        renderer_upload_land(renderer, land);
    }
}

/* Recompute distance/azimuth and rebuild target geometry (gc line + projections).
 * Pass recompute_dist=1 when target changed, 0 when only projection/center changed. */
static void update_target_geometry(double center_lat, double center_lon,
                                   double target_lat, double target_lon,
                                   double *dist, double *az_to, double *az_from,
                                   double *cx, double *cy, double *tx, double *ty,
                                   Renderer *renderer, int recompute_dist)
{
    if (recompute_dist) {
        *dist = projection_distance(center_lat, center_lon, target_lat, target_lon);
        *az_to = projection_azimuth(center_lat, center_lon, target_lat, target_lon);
        *az_from = projection_azimuth(target_lat, target_lon, center_lat, center_lon);
    }
    projection_forward(center_lat, center_lon, cx, cy);
    projection_forward(target_lat, target_lon, tx, ty);
    float gc_verts[GC_LINE_POINTS * 2];
    int gc_n = build_gc_line(center_lat, center_lon, target_lat, target_lon, gc_verts);
    renderer_upload_target_line(renderer, gc_verts, gc_n);
}

/* Parse pipe-delimited detail string into ui->station_info[]. */
static void parse_station_detail(UI *ui, const char *detail_str)
{
    static const char *labels[] = {"STN", "FREQ", "CTRY", "SITE", "LANG", "TGT"};
    char dbuf[256];
    strncpy(dbuf, detail_str, sizeof(dbuf) - 1);
    dbuf[sizeof(dbuf) - 1] = '\0';
    char *field = dbuf;
    ui->station_info_lines = 0;
    for (int i = 0; i < 6 && field; i++) {
        char *next = strchr(field, '|');
        if (next) *next = '\0';
        if (*field) {
            char upper[40];
            str_upper(upper, sizeof(upper), field);
            snprintf(ui->station_info[ui->station_info_lines],
                     sizeof(ui->station_info[0]),
                     "%s: %s", labels[i], upper);
            ui->station_info_lines++;
        }
        field = next ? next + 1 : NULL;
    }
}

static void print_usage(const char *prog)
{
    fprintf(stderr,
        "Usage: %s <center_lat> <center_lon> <target_lat> <target_lon> [options]\n"
        "       %s <target_lat> <target_lon> [options]  (center from config)\n"
        "       %s [options]                             (restore saved session)\n"
        "\n"
        "  center_lat/lon  Center of azimuthal equidistant projection (degrees)\n"
        "  target_lat/lon  Second location to draw a line to (degrees)\n"
        "\n"
        "Options:\n"
        "  -c NAME    Center location name\n"
        "  -t NAME    Target location name\n"
        "  -s PATH    Shapefile path override (default: %s)\n"
        "\n"
        "Config file: ~/.config/azmap.conf\n"
        "  name = Madrid\n"
        "  lat = 40.4168\n"
        "  lon = -3.7038\n"
        "\n"
        "Controls:\n"
        "  Scroll       Zoom in/out\n"
        "  Drag         Pan the map\n"
        "  Arrow keys   Pan the map\n"
        "  R            Reset view\n"
        "  Q / Esc      Quit\n",
        prog, prog, prog, DEFAULT_SHP_REL);
}

int main(int argc, char **argv)
{
    /* Load config file (optional) */
    Config cfg;
    int has_config = (config_load(&cfg) == 0 && cfg.valid);

    double center_lat, center_lon, target_lat, target_lon;
    const char *center_name = NULL;
    const char *target_name = NULL;
    char target_name_buf[64] = {0}; /* mutable buffer for QRZ-updated target name */
    const char *shp_override = NULL;

    /* Determine how many positional args we have (before any -flag).
     * Negative numbers (e.g. -3.7038) are positional, not flags. */
    int npos = 0;
    for (int j = 1; j < argc; j++) {
        if (argv[j][0] == '-' && !isdigit((unsigned char)argv[j][1]) && argv[j][1] != '.')
            break;
        npos++;
    }

    int opt_start; /* index where flag parsing begins */
    int cli_center_given = 0; /* set when user passed explicit center coords */

    if (npos >= 4) {
        /* Full mode: center + target from CLI */
        center_lat = atof(argv[1]);
        center_lon = atof(argv[2]);
        target_lat = atof(argv[3]);
        target_lon = atof(argv[4]);
        opt_start = 5;
        cli_center_given = 1;
    } else if (npos >= 2 && has_config) {
        /* Config mode: center from config, target from CLI */
        center_lat = cfg.lat;
        center_lon = cfg.lon;
        if (cfg.name[0])
            center_name = cfg.name;
        target_lat = atof(argv[1]);
        target_lon = atof(argv[2]);
        opt_start = 3;
    } else if (npos == 0 && has_config && cfg.target_valid) {
        /* Zero-arg mode: center from config, target from saved state */
        center_lat = cfg.lat;
        center_lon = cfg.lon;
        if (cfg.name[0])
            center_name = cfg.name;
        target_lat = cfg.target_lat;
        target_lon = cfg.target_lon;
        if (cfg.target_name[0])
            target_name = cfg.target_name;
        opt_start = 1;
    } else {
        if (npos >= 2 && !has_config)
            fprintf(stderr, "Error: 2 args given but no valid config file found.\n"
                            "Create ~/.config/azmap.conf with lat and lon, or pass 4 positional args.\n\n");
        print_usage(argv[0]);
        return 1;
    }

    /* Parse optional flags */
    const char *detail_arg = NULL;
    int argi = opt_start;
    while (argi < argc) {
        if (strcmp(argv[argi], "-c") == 0 && argi + 1 < argc) {
            center_name = argv[++argi];
        } else if (strcmp(argv[argi], "-t") == 0 && argi + 1 < argc) {
            target_name = argv[++argi];
        } else if (strcmp(argv[argi], "-d") == 0 && argi + 1 < argc) {
            detail_arg = argv[++argi];
        } else if (strcmp(argv[argi], "-s") == 0 && argi + 1 < argc) {
            shp_override = argv[++argi];
        } else if (argv[argi][0] != '-' && !shp_override) {
            /* Backward compat: bare arg = shapefile path */
            shp_override = argv[argi];
        } else {
            fprintf(stderr, "Unknown option: %s\n", argv[argi]);
            print_usage(argv[0]);
            return 1;
        }
        argi++;
    }

    /* Resolve default paths relative to executable location */
    char exe_path[PATH_MAX];
    ssize_t len = readlink("/proc/self/exe", exe_path, sizeof(exe_path) - 1);
    if (len == -1) {
        strncpy(exe_path, argv[0], PATH_MAX - 1);
        exe_path[PATH_MAX - 1] = '\0';
    } else {
        exe_path[len] = '\0';
    }

    char default_shp[PATH_MAX], default_border[PATH_MAX], default_land[PATH_MAX], shader_dir[PATH_MAX];
    resolve_path(exe_path, DEFAULT_SHP_REL, default_shp, sizeof(default_shp));
    resolve_path(exe_path, DEFAULT_BORDER_REL, default_border, sizeof(default_border));
    resolve_path(exe_path, DEFAULT_LAND_REL, default_land, sizeof(default_land));
    resolve_path(exe_path, DEFAULT_SHADER_REL, shader_dir, sizeof(shader_dir));

    const char *shp_path = shp_override ? shp_override : default_shp;

    /* Restore saved view center if CLI didn't specify center */
    if (!cli_center_given && cfg.view_valid) {
        center_lat = cfg.view_center_lat;
        center_lon = cfg.view_center_lon;
    }

    /* Restore projection mode before any geometry is built */
    if (cfg.view_valid && cfg.view_proj_mode == 1)
        projection_set_mode(PROJ_ORTHO);

    /* Set up projection */
    projection_set_center(center_lat, center_lon);

    /* Project original center and target points */
    double cx = 0.0, cy = 0.0;  /* original center in projected space */
    double tx, ty;
    projection_forward(target_lat, target_lon, &tx, &ty);

    double dist = projection_distance(center_lat, center_lon, target_lat, target_lon);
    double az_to = projection_azimuth(center_lat, center_lon, target_lat, target_lon);
    double az_from = projection_azimuth(target_lat, target_lon, center_lat, center_lon);
    printf("Center:   %.4f, %.4f\n", center_lat, center_lon);
    printf("Target:   %.4f, %.4f\n", target_lat, target_lon);
    printf("Distance: %.1f km\n", dist);
    printf("Az to:    %.1f deg\n", az_to);
    printf("Az from:  %.1f deg\n", az_from);

    /* Build label strings */
    char center_label[128], target_label[128];
    build_label(center_label, sizeof(center_label), center_name, center_lat, center_lon);
    build_label(target_label, sizeof(target_label), target_name, target_lat, target_lon);

    /* QRZ API init */
    int has_qrz = 0;
    if (cfg.qrz_user[0] && cfg.qrz_pass[0]) {
        if (qrz_init(cfg.qrz_user, cfg.qrz_pass) == 0)
            has_qrz = 1;
    }

    /* Init GLFW */
    if (!glfwInit()) {
        fprintf(stderr, "Error: GLFW init failed\n");
        return 1;
    }

    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_SAMPLES, 4);
    glfwWindowHint(GLFW_STENCIL_BITS, 8);

    int init_w = (cfg.window_valid) ? cfg.window_w : DEFAULT_WIDTH;
    int init_h = (cfg.window_valid) ? cfg.window_h : DEFAULT_HEIGHT;
    GLFWwindow *window = glfwCreateWindow(init_w, init_h, "azMap", NULL, NULL);
    if (!window) {
        fprintf(stderr, "Error: window creation failed\n");
        glfwTerminate();
        return 1;
    }
    glfwMakeContextCurrent(window);
    glfwSwapInterval(1);

    /* Init GLEW — ignore GLEW_ERROR_NO_GLX_DISPLAY on Wayland.
     * GLEW 2.1+ defines GLEW_ERROR_NO_GLX_DISPLAY = 4; use literal
     * for compatibility with older GLEW versions. */
    glewExperimental = GL_TRUE;
    GLenum glew_err = glewInit();
    #ifndef GLEW_ERROR_NO_GLX_DISPLAY
    #define GLEW_ERROR_NO_GLX_DISPLAY 4
    #endif
    if (glew_err != GLEW_OK && glew_err != GLEW_ERROR_NO_GLX_DISPLAY) {
        fprintf(stderr, "Error: GLEW init failed: %s\n", glewGetErrorString(glew_err));
        glfwTerminate();
        return 1;
    }
    while (glGetError() != GL_NO_ERROR); /* Clear spurious GL errors from GLEW */

    glEnable(GL_MULTISAMPLE);

    /* Init renderer */
    Renderer renderer;
    if (renderer_init(&renderer, shader_dir) != 0) {
        fprintf(stderr, "Error: renderer init failed\n");
        glfwTerminate();
        return 1;
    }

    /* Load map data */
    MapData map;
    if (map_data_load(&map, shp_path) != 0) {
        fprintf(stderr, "Error: failed to load shapefile: %s\n", shp_path);
        glfwTerminate();
        return 1;
    }

    /* Load country borders (optional) */
    MapData borders;
    int has_borders = (map_data_load(&borders, default_border) == 0);
    if (!has_borders)
        printf("Note: country borders not found, skipping. Download ne_110m_admin_0_boundary_lines_land.\n");

    /* Load land polygons (optional — no segment splitting for stencil fill) */
    MapData land;
    int has_land = (map_data_load(&land, default_land) == 0);
    if (has_land)
        map_data_reproject_nosplit(&land);
    else
        printf("Note: land polygons not found, skipping. Download ne_110m_land.\n");

    /* Build grid (graticule) — mode-appropriate */
    MapData grid;
    memset(&grid, 0, sizeof(grid));
    if (projection_get_mode() == PROJ_ORTHO)
        grid_build_geo(&grid);
    else
        grid_build(&grid);

    /* Night overlay */
    NightMesh nightmesh;
    nightmesh_init(&nightmesh);

    /* MUF / Aurora overlays */
    MufData muf_data;
    muf_data_init(&muf_data);
    AuroraGrid aurora_grid;
    aurora_grid_init(&aurora_grid);
    AuroraMesh aurora_mesh;
    aurora_mesh_init(&aurora_mesh);
    FetchRequest muf_fetch, aurora_fetch;
    memset(&muf_fetch, 0, sizeof(muf_fetch));
    memset(&aurora_fetch, 0, sizeof(aurora_fetch));
    int muf_active = 0, aurora_active = 0;
    int muf_fetching = 0, aurora_fetching = 0;
    time_t last_muf_fetch = 0, last_aurora_fetch = 0;
    GeomagIndices geomag;
    geomag_init(&geomag);
    FetchRequest kp_fetch, bz_fetch;
    memset(&kp_fetch, 0, sizeof(kp_fetch));
    memset(&bz_fetch, 0, sizeof(bz_fetch));
    int kp_fetching = 0, bz_fetching = 0;
    time_t last_geomag_fetch = 0;

    /* Upload geometry to GPU */
    renderer_upload_map(&renderer, &map);
    if (has_borders)
        renderer_upload_borders(&renderer, &borders);
    if (has_land)
        renderer_upload_land(&renderer, &land);
    renderer_upload_grid(&renderer, &grid);
    {
        float gc_verts[GC_LINE_POINTS * 2];
        int gc_n = build_gc_line(center_lat, center_lon, target_lat, target_lon, gc_verts);
        renderer_upload_target_line(&renderer, gc_verts, gc_n);
    }
    renderer_upload_earth_circle(&renderer, projection_get_radius());

    /* North pole marker */
    double npx, npy;
    projection_forward(90.0, 0.0, &npx, &npy);

    /* Text overlay (rebuilt each second for clock update) */
    text_init();
    float text_verts[8192];

    /* UI buttons */
    UI ui;
    ui_init(&ui);
    ui.sidebar_visible = 1;

    /* Parse -d detail string: "station|freq|country|site|lang|target" */
    if (detail_arg)
        parse_station_detail(&ui, detail_arg);
    int btn_home    = ui_add_button(&ui, "Home",   0, 0, 80, 28);
    int btn_proj    = ui_add_button(&ui, "Proj",   0, 0, 80, 28);
    /* Sidebar buttons — layers row */
    int btn_aurora = ui_add_button(&ui, "Aurora",  0, 0, 80, 28);
    int btn_spore  = ui_add_button(&ui, "Spor.E",  0, 0, 80, 28);
    int btn_muf    = ui_add_button(&ui, "MUF",     0, 0, 80, 28);
    /* Sidebar buttons — mode row */
    int btn_opt1 = ui_add_button(&ui, "QRZ",  0, 0, 80, 28);
    int btn_opt2 = ui_add_button(&ui, "WSJT", 0, 0, 80, 28);
    int btn_opt3 = ui_add_button(&ui, "BCB",  0, 0, 80, 28);

    /* Camera — use actual framebuffer size (differs from window size on HiDPI) */
    Camera cam;
    camera_init(&cam);

    /* Restore saved camera state */
    if (cfg.view_valid) {
        cam.zoom_km = cfg.view_zoom_km;
        cam.pan_x = cfg.view_pan_x;
        cam.pan_y = cfg.view_pan_y;
        double max_diam = 2.0 * projection_get_radius();
        if (cam.zoom_km > (float)max_diam) cam.zoom_km = (float)max_diam;
        if (cam.zoom_km < 10.0f) cam.zoom_km = 10.0f;
    }

    int fb_w, fb_h;
    glfwGetFramebufferSize(window, &fb_w, &fb_h);
    glViewport(0, 0, fb_w, fb_h);
    cam.aspect = (float)fb_w / (float)fb_h;

    /* Marker size: proportional to Earth for initial view */
    float marker_size = 300.0f;
    renderer_upload_markers(&renderer, 0.0f, 0.0f, (float)tx, (float)ty, marker_size);

    /* Input */
    InputState input;
    input_init(&input, window, &cam, &ui, center_lat, center_lon);

    /* Label vertex buffer (rebuilt each frame) */
    float label_verts[8192];

    /* Night overlay timer (outside loop so center-dirty can reset it) */
    time_t last_sun_update = 0;
    /* HUD text timer (outside loop so QRZ can force rebuild) */
    time_t last_text_update = 0;

    /* Named pipe for IPC (swl dashboard → azMap target updates) */
    #define FIFO_PATH "/tmp/azmap-target.fifo"
    mkfifo(FIFO_PATH, 0666); /* no-op if already exists */
    int fifo_fd = open(FIFO_PATH, O_RDWR | O_NONBLOCK);
    /* O_RDWR keeps fd alive even when writer closes */
    char fifo_buf[512];
    int fifo_buf_len = 0;

    /* Main loop */
    while (!glfwWindowShouldClose(window)) {
        glfwPollEvents();

        /* Check named pipe for target updates from swl dashboard */
        if (fifo_fd >= 0) {
            ssize_t nr = read(fifo_fd, fifo_buf + fifo_buf_len,
                              sizeof(fifo_buf) - 1 - fifo_buf_len);
            if (nr > 0) {
                fifo_buf_len += (int)nr;
                fifo_buf[fifo_buf_len] = '\0';
                /* Process last complete line (delimited by newline) */
                char *last_nl = strrchr(fifo_buf, '\n');
                if (!last_nl && fifo_buf_len >= (int)sizeof(fifo_buf) - 2) {
                    /* Buffer full with no newline — discard to avoid stall */
                    fifo_buf_len = 0;
                } else if (last_nl) {
                    *last_nl = '\0';
                    /* Find start of last complete line */
                    char *line = strrchr(fifo_buf, '\n');
                    line = line ? line + 1 : fifo_buf;
                    /* Parse "lat,lon,name|station|freq|country|site|lang|target" */
                    double new_lat, new_lon;
                    char new_name[64] = {0};
                    if (sscanf(line, "%lf,%lf,", &new_lat, &new_lon) == 2) {
                        /* Extract name after second comma (up to first '|') */
                        char *p = strchr(line, ',');
                        if (p) p = strchr(p + 1, ',');
                        if (p && *(p + 1)) {
                            p++; /* skip comma */
                            char *pipe = strchr(p, '|');
                            if (pipe) {
                                size_t nlen = (size_t)(pipe - p);
                                if (nlen >= sizeof(new_name)) nlen = sizeof(new_name) - 1;
                                memcpy(new_name, p, nlen);
                                new_name[nlen] = '\0';
                                /* Parse station detail fields after first '|' */
                                parse_station_detail(&ui, pipe + 1);
                            } else {
                                strncpy(new_name, p, sizeof(new_name) - 1);
                            }
                        }
                        /* Update target */
                        target_lat = new_lat;
                        target_lon = new_lon;
                        strncpy(target_name_buf, new_name, sizeof(target_name_buf) - 1);
                        target_name_buf[sizeof(target_name_buf) - 1] = '\0';
                        target_name = target_name_buf;
                        build_label(target_label, sizeof(target_label),
                                    target_name, target_lat, target_lon);
                        update_target_geometry(center_lat, center_lon,
                                               target_lat, target_lon,
                                               &dist, &az_to, &az_from,
                                               &cx, &cy, &tx, &ty,
                                               &renderer, 1);
                        last_text_update = 0; /* force HUD refresh */
                    }
                    /* Discard processed data */
                    fifo_buf_len = 0;
                }
            }
        }

        /* Handle projection center change (drag / arrow keys) */
        if (input.center_dirty) {
            input.center_dirty = 0;
            projection_set_center(input.center_lat, input.center_lon);
            reproject_all(&map, &borders, has_borders, &land, has_land, &renderer);
            update_target_geometry(center_lat, center_lon,
                                   target_lat, target_lon,
                                   &dist, &az_to, &az_from,
                                   &cx, &cy, &tx, &ty,
                                   &renderer, 0);
            projection_forward(90.0, 0.0, &npx, &npy);
            /* In ortho mode, grid depends on projection center */
            if (projection_get_mode() == PROJ_ORTHO) {
                grid_build_geo(&grid);
                renderer_upload_grid(&renderer, &grid);
            }
            last_sun_update = 0; /* force night mesh rebuild */
            /* Reproject overlays */
            if (muf_active && muf_data.raw_count > 0) {
                muf_reproject(&muf_data);
                renderer_upload_muf(&renderer, &muf_data);
            }
            if (aurora_active && aurora_grid.valid) {
                aurora_mesh_build(&aurora_mesh, &aurora_grid);
                renderer_upload_aurora(&renderer, &aurora_mesh);
            }
        }

        /* Update marker size relative to zoom */
        float ms = cam.zoom_km * MARKER_ZOOM_FACTOR;
        if (dist > 0.0)
            renderer_upload_markers(&renderer, (float)cx, (float)cy, (float)tx, (float)ty, ms);
        else
            renderer_upload_markers(&renderer, (float)cx, (float)cy, (float)cx, (float)cy, 0.0f);
        renderer_upload_npole(&renderer, (float)npx, (float)npy, ms);

        glfwGetFramebufferSize(window, &fb_w, &fb_h);

        /* Compute sidebar and map area widths */
        int sidebar_fb_w = 0;
        if (ui.sidebar_visible) {
            sidebar_fb_w = (int)(SIDEBAR_WIDTH_PX * input.cursor_scale_x);
            if (sidebar_fb_w > fb_w / 2) sidebar_fb_w = fb_w / 2;
        }
        ui.sidebar_fb_w = sidebar_fb_w;
        int map_fb_w = fb_w - sidebar_fb_w;

        glViewport(0, 0, map_fb_w, fb_h);
        cam.aspect = (float)map_fb_w / (float)fb_h;

        float mvp[16];
        camera_get_mvp(&cam, mvp);

        /* Build labels at screen positions of center and target markers */
        float label_size = 14.0f;
        float cpx, cpy, tpx, tpy;
        km_to_pixel(mvp, (float)cx, (float)cy, map_fb_w, fb_h, &cpx, &cpy);
        km_to_pixel(mvp, (float)tx, (float)ty, map_fb_w, fb_h, &tpx, &tpy);

        /* Center label: offset above center marker */
        float cw = text_width(center_label, label_size);
        float clx = cpx - cw * 0.5f;
        float cly = cpy - label_size * 1.8f;
        int center_vcount = text_build(center_label, clx, cly,
            label_size, label_verts, 4096);
        /* Target label: offset below target crosshair (only if target active) */
        int target_vcount = 0;
        float tw = 0, tlx = 0, tly = 0;
        if (dist > 0.0) {
            tw = text_width(target_label, label_size);
            tlx = tpx - tw * 0.5f;
            tly = tpy + label_size * 0.8f;
            target_vcount = text_build(target_label, tlx, tly,
                label_size, label_verts + center_vcount * 2, 4096 - center_vcount);
        }

        renderer_upload_labels(&renderer, label_verts, center_vcount + target_vcount, center_vcount);

        /* Label backgrounds */
        float bg_verts[24]; /* 2 quads * 6 verts * 2 floats */
        float pad = 4.0f;
        int cbg = build_label_bg(clx, cly, cw, label_size, pad, bg_verts);
        int tbg = (dist > 0.0) ? build_label_bg(tlx, tly, tw, label_size, pad, bg_verts + cbg * 2) : 0;
        renderer_upload_label_bgs(&renderer, bg_verts, cbg + tbg, cbg);

        /* Update button positions */
        {
            float bh = BUTTON_HEIGHT, margin = 10.0f;

            /* Proj button: upper-left corner of map */
            ui.buttons[btn_proj].x = margin;
            ui.buttons[btn_proj].y = margin;

            /* Home button: bottom-center of map */
            ui.buttons[btn_home].x = ((float)map_fb_w - ui.buttons[btn_home].w) * 0.5f;
            ui.buttons[btn_home].y = (float)fb_h - bh - margin;

            /* Sidebar buttons: positioned in full-window framebuffer coords.
             * Layout from bottom up:
             *   SOURCE label + line + [QRZ] [WSJT] [BCB]
             *   LAYERS label + line + [AURORA] [SPOR.E] [MUF]
             */
            if (sidebar_fb_w > 0) {
                float sb_left = (float)map_fb_w;
                float sbw = (float)sidebar_fb_w;
                float sb_gap = 6.0f;
                float sb_margin = 8.0f;
                float label_h = 16.0f;  /* section label font size */
                float line_gap = 4.0f;  /* gap between line and buttons */
                float section_gap = 14.0f; /* gap between sections */

                /* Start from bottom */
                float by = (float)fb_h - bh - sb_margin;

                /* Bottom row: QRZ, WSJT, BCB (SOURCE section) */
                int bottom_btns[] = { btn_opt1, btn_opt2, btn_opt3 };
                float row_w = 0;
                for (int i = 0; i < 3; i++) row_w += ui.buttons[bottom_btns[i]].w;
                row_w += 2 * sb_gap;
                float rx = sb_left + (sbw - row_w) * 0.5f;
                for (int i = 0; i < 3; i++) {
                    ui.buttons[bottom_btns[i]].x = rx;
                    ui.buttons[bottom_btns[i]].y = by;
                    rx += ui.buttons[bottom_btns[i]].w + sb_gap;
                }
                /* SOURCE label + line sits above the buttons */
                ui.section_modes_y = by - line_gap - 2.0f; /* line Y (full-window) */
                ui.section_modes_label_y = ui.section_modes_y - label_h - 8.0f;

                /* Upper row: Aurora, Spor.E, MUF (LAYERS section) */
                by = ui.section_modes_label_y - section_gap - bh;
                int upper_btns[] = { btn_aurora, btn_spore, btn_muf };
                row_w = 0;
                for (int i = 0; i < 3; i++) row_w += ui.buttons[upper_btns[i]].w;
                row_w += 2 * sb_gap;
                rx = sb_left + (sbw - row_w) * 0.5f;
                for (int i = 0; i < 3; i++) {
                    ui.buttons[upper_btns[i]].x = rx;
                    ui.buttons[upper_btns[i]].y = by;
                    rx += ui.buttons[upper_btns[i]].w + sb_gap;
                }
                /* LAYERS label + line sits above the buttons */
                ui.section_layers_y = by - line_gap - 2.0f;
                ui.section_layers_label_y = ui.section_layers_y - label_h - 8.0f;
            }
        }

        /* Build and upload button geometry */
        {
            float btn_quads[16 * 84 * 2]; /* rounded rect: ~84 verts * 2 floats per button */
            float btn_outlines[16 * 56 * 2]; /* outline: ~56 verts * 2 floats per button */
            float btn_text[8192];
            int quad_count, outline_count, text_count, hovered_quad;
            int btn_offsets[16], btn_counts[16];
            int ol_offsets[16], ol_counts[16];
            ui_build_geometry(&ui, btn_quads, &quad_count,
                              btn_offsets, btn_counts,
                              btn_outlines, &outline_count,
                              ol_offsets, ol_counts,
                              btn_text, &text_count, &hovered_quad);

            /* Add section labels and horizontal lines to button text buffer */
            if (sidebar_fb_w > 0) {
                float sb_left = (float)map_fb_w;
                float sbw = (float)sidebar_fb_w;
                float lsz = 14.0f;  /* label font size */
                float line_inset = 12.0f;

                /* "LAYERS" label */
                float lw = text_width("LAYERS", lsz);
                text_count += text_build("LAYERS",
                    sb_left + (sbw - lw) * 0.5f, ui.section_layers_label_y,
                    lsz, btn_text + text_count * 2, 8192 - text_count);
                /* Horizontal line below LAYERS */
                int li = text_count * 2;
                btn_text[li+0] = sb_left + line_inset;
                btn_text[li+1] = ui.section_layers_y;
                btn_text[li+2] = sb_left + sbw - line_inset;
                btn_text[li+3] = ui.section_layers_y;
                text_count += 2;

                /* "SOURCE" label */
                lw = text_width("SOURCE", lsz);
                text_count += text_build("SOURCE",
                    sb_left + (sbw - lw) * 0.5f, ui.section_modes_label_y,
                    lsz, btn_text + text_count * 2, 8192 - text_count);
                /* Horizontal line below SOURCE */
                li = text_count * 2;
                btn_text[li+0] = sb_left + line_inset;
                btn_text[li+1] = ui.section_modes_y;
                btn_text[li+2] = sb_left + sbw - line_inset;
                btn_text[li+3] = ui.section_modes_y;
                text_count += 2;

                /* MUF contour legend above LAYERS label */
                int have_legend = (muf_active && muf_data.legend_count > 0);
                int have_geomag = (aurora_active && geomag.valid);
                if (have_legend || have_geomag) {
                    float leg_sz = 14.0f;
                    float swatch_w = 24.0f;
                    float gap = 6.0f;
                    float leg_line_h = leg_sz * 1.4f;
                    float leg_line_verts[MUF_MAX_LEGEND * 4]; /* 2 verts * 2 floats */
                    float leg_colors[MUF_MAX_LEGEND][4];
                    float leg_text_verts[4096];
                    int ltvc = 0;
                    int nc = 0;
                    float leg_left = sb_left + line_inset;
                    float leg_y = ui.section_layers_label_y - leg_sz - 18.0f;

                    /* MUF legend entries */
                    if (have_legend) {
                        nc = muf_data.legend_count;
                        for (int ei = nc - 1; ei >= 0; ei--) {
                            char label[16];
                            if (muf_data.legend[ei].mhz == (int)muf_data.legend[ei].mhz)
                                snprintf(label, sizeof(label), "%.0f MHz",
                                         (double)muf_data.legend[ei].mhz);
                            else
                                snprintf(label, sizeof(label), "%.1f MHz",
                                         (double)muf_data.legend[ei].mhz);

                            /* Colored swatch line (left-aligned) */
                            int vi = ei * 4;
                            leg_line_verts[vi + 0] = leg_left;
                            leg_line_verts[vi + 1] = leg_y + leg_sz * 0.5f;
                            leg_line_verts[vi + 2] = leg_left + swatch_w;
                            leg_line_verts[vi + 3] = leg_y + leg_sz * 0.5f;
                            memcpy(leg_colors[ei], muf_data.legend[ei].color, sizeof(float) * 4);

                            /* Text label */
                            ltvc += text_build(label, leg_left + swatch_w + gap, leg_y,
                                               leg_sz, leg_text_verts + ltvc * 2,
                                               2048 - ltvc);
                            leg_y -= leg_line_h;
                        }
                    }

                    /* Kp/Bz indices (right side of sidebar) */
                    if (have_geomag) {
                        float geo_right = sb_left + sbw - line_inset;
                        float geo_y = ui.section_layers_label_y - leg_sz - 18.0f;
                        char kp_label[32], bz_label[32];
                        snprintf(kp_label, sizeof(kp_label), "Kp %.1f",
                                 (double)geomag.kp);
                        snprintf(bz_label, sizeof(bz_label), "Bz %.1f nT",
                                 (double)geomag.bz);
                        /* Right-align: position text so it ends at geo_right */
                        float kp_w = text_width(kp_label, leg_sz);
                        ltvc += text_build(kp_label, geo_right - kp_w, geo_y,
                                           leg_sz, leg_text_verts + ltvc * 2,
                                           2048 - ltvc);
                        geo_y -= leg_line_h;
                        float bz_w = text_width(bz_label, leg_sz);
                        ltvc += text_build(bz_label, geo_right - bz_w, geo_y,
                                           leg_sz, leg_text_verts + ltvc * 2,
                                           2048 - ltvc);
                    }

                    renderer_upload_legend(&renderer, leg_line_verts, leg_colors,
                                           nc, leg_text_verts, ltvc);
                } else {
                    renderer.legend_line_count = 0;
                    renderer.legend_text_vertex_count = 0;
                }
            }

            /* Compute active button mask (bitmask by visible-button index) */
            unsigned int active_mask = 0;
            {
                int vis = 0;
                for (int bi = 0; bi < ui.count; bi++) {
                    if (!ui.buttons[bi].visible) continue;
                    if ((bi == btn_proj && projection_get_mode() == PROJ_ORTHO) ||
                        (bi == btn_aurora && aurora_active) ||
                        (bi == btn_muf && muf_active))
                        active_mask |= (1u << vis);
                    vis++;
                }
            }
            /* Count visible buttons */
            int nvis = 0;
            for (int bi = 0; bi < ui.count; bi++)
                if (ui.buttons[bi].visible) nvis++;
            if (quad_count > 0 || text_count > 0)
                renderer_upload_buttons(&renderer, btn_quads, quad_count,
                                        btn_offsets, btn_counts,
                                        btn_outlines, outline_count,
                                        ol_offsets, ol_counts,
                                        btn_text, text_count,
                                        nvis, hovered_quad, active_mask);
        }

        /* Build and upload popup geometry */
        if (ui.popup.visible) {
            float popup_quads[5 * 12]; /* up to 5 quads * 6 verts * 2 floats */
            float popup_text[8192];
            int pq_count, pt_count;
            ui_build_popup_geometry(&ui, map_fb_w, fb_h,
                                    popup_quads, &pq_count,
                                    popup_text, &pt_count);
            renderer_upload_popup(&renderer, popup_quads, pq_count,
                                  popup_text, pt_count,
                                  ui.popup_close_hovered);
        } else {
            renderer.popup_bg_vertex_count = 0;
            renderer.popup_text_vertex_count = 0;
        }

        /* Poll button clicks */
        if (ui.clicked >= 0) {
            printf("Button clicked: %s\n", ui.buttons[ui.clicked].label);
            if (ui.clicked == btn_proj) {
                /* Toggle projection mode */
                ProjMode cur = projection_get_mode();
                ProjMode nxt = (cur == PROJ_AZEQ) ? PROJ_ORTHO : PROJ_AZEQ;
                projection_set_mode(nxt);
                reproject_all(&map, &borders, has_borders, &land, has_land, &renderer);
                /* Re-project key points */
                update_target_geometry(center_lat, center_lon,
                                       target_lat, target_lon,
                                       &dist, &az_to, &az_from,
                                       &cx, &cy, &tx, &ty,
                                       &renderer, 0);
                projection_forward(90.0, 0.0, &npx, &npy);
                /* Rebuild grid for new mode */
                if (nxt == PROJ_ORTHO)
                    grid_build_geo(&grid);
                else
                    grid_build(&grid);
                renderer_upload_grid(&renderer, &grid);
                /* Rebuild earth circle and disc */
                renderer_upload_earth_circle(&renderer, projection_get_radius());
                /* Force night mesh rebuild */
                last_sun_update = 0;
                /* Reproject overlays */
                if (muf_active && muf_data.raw_count > 0) {
                    muf_reproject(&muf_data);
                    renderer_upload_muf(&renderer, &muf_data);
                }
                if (aurora_active && aurora_grid.valid) {
                    aurora_mesh_build(&aurora_mesh, &aurora_grid);
                    renderer_upload_aurora(&renderer, &aurora_mesh);
                }
                /* Clamp zoom */
                double max_diam = 2.0 * projection_get_radius();
                if (cam.zoom_km > (float)max_diam)
                    cam.zoom_km = (float)max_diam;
            } else if (ui.clicked == btn_opt1) {
                /* QRZ: clear info, open callsign lookup popup */
                clear_target_state(&ui, &dist, &az_to, &az_from,
                                   &renderer, &last_text_update);
                if (!has_qrz) {
                    ui_show_popup(&ui, "QRZ LOOKUP");
                    strncpy(ui.popup_result[0], "NO QRZ CREDENTIALS", sizeof(ui.popup_result[0]) - 1);
                    strncpy(ui.popup_result[1], "IN CONFIG", sizeof(ui.popup_result[1]) - 1);
                    ui.popup_result_lines = 2;
                    ui.popup_input_active = 0;
                } else {
                    ui_show_popup(&ui, "QRZ LOOKUP");
                }
            } else if (ui.clicked == btn_opt2) {
                /* WSJT: clear info, show popup */
                clear_target_state(&ui, &dist, &az_to, &az_from,
                                   &renderer, &last_text_update);
                ui_show_popup(&ui, "WSJT");
            } else if (ui.clicked == btn_opt3) {
                /* BCB: just clear info */
                clear_target_state(&ui, &dist, &az_to, &az_from,
                                   &renderer, &last_text_update);
            } else if (ui.clicked == btn_aurora) {
                aurora_active = !aurora_active;
                if (aurora_active) {
                    if (aurora_grid.valid) {
                        /* Re-upload existing data */
                        aurora_mesh_build(&aurora_mesh, &aurora_grid);
                        renderer_upload_aurora(&renderer, &aurora_mesh);
                    } else if (!aurora_fetching) {
                        fetch_start(&aurora_fetch, AURORA_URL);
                        aurora_fetching = 1;
                        last_aurora_fetch = time(NULL);
                    }
                    /* Fetch Kp/Bz indices if not already fetched */
                    if (!geomag.valid && !kp_fetching) {
                        fetch_start(&kp_fetch, KP_URL);
                        kp_fetching = 1;
                    }
                    if (!geomag.valid && !bz_fetching) {
                        fetch_start(&bz_fetch, BZ_URL);
                        bz_fetching = 1;
                    }
                    last_geomag_fetch = time(NULL);
                } else {
                    renderer.aurora_vertex_count = 0;
                }
            } else if (ui.clicked == btn_muf) {
                muf_active = !muf_active;
                if (muf_active) {
                    if (muf_data.raw_count > 0) {
                        /* Re-upload existing data */
                        renderer_upload_muf(&renderer, &muf_data);
                    } else if (!muf_fetching) {
                        fetch_start(&muf_fetch, MUF_URL);
                        muf_fetching = 1;
                        last_muf_fetch = time(NULL);
                    }
                } else {
                    renderer.muf_num_segments = 0;
                }
            } else if (ui.clicked == btn_home) {
                ui_hide_popup(&ui);
            }
            ui.clicked = -1;
        }

        /* Handle QRZ popup submission */
        if (ui.popup_submitted) {
            ui.popup_submitted = 0;
            QRZResult qrz_result;
            char err_buf[128];
            if (qrz_lookup(ui.popup_input, &qrz_result, err_buf, sizeof(err_buf)) == 0
                && qrz_result.valid) {
                /* Update target */
                target_lat = qrz_result.lat;
                target_lon = qrz_result.lon;
                strncpy(target_name_buf, qrz_result.call, sizeof(target_name_buf) - 1);
                target_name = target_name_buf;
                build_label(target_label, sizeof(target_label),
                            target_name, target_lat, target_lon);
                update_target_geometry(center_lat, center_lon,
                                       target_lat, target_lon,
                                       &dist, &az_to, &az_from,
                                       &cx, &cy, &tx, &ty,
                                       &renderer, 1);
                /* Close popup and show results in sidebar */
                ui_hide_popup(&ui);
                last_text_update = 0;

                /* Fill station_info for sidebar display */
                ui.station_info_lines = 0;
                snprintf(ui.station_info[ui.station_info_lines++],
                         sizeof(ui.station_info[0]), "CALL: %s", qrz_result.call);
                {
                    char upper[40];
                    str_upper(upper, sizeof(upper), qrz_result.name);
                    snprintf(ui.station_info[ui.station_info_lines++],
                             sizeof(ui.station_info[0]), "NAME: %s", upper);
                    str_upper(upper, sizeof(upper), qrz_result.location);
                    snprintf(ui.station_info[ui.station_info_lines++],
                             sizeof(ui.station_info[0]), "LOC: %s", upper);
                    char upper_grid[16];
                    str_upper(upper_grid, sizeof(upper_grid), qrz_result.grid);
                    snprintf(ui.station_info[ui.station_info_lines++],
                             sizeof(ui.station_info[0]), "GRID: %s", upper_grid);
                    char coord[64];
                    format_coord(coord, sizeof(coord), qrz_result.lat, qrz_result.lon);
                    snprintf(ui.station_info[ui.station_info_lines++],
                             sizeof(ui.station_info[0]), "%.47s", coord);
                }
            } else {
                /* Error */
                str_upper(ui.popup_result[0], sizeof(ui.popup_result[0]), err_buf);
                ui.popup_result_lines = 1;
            }
        }

        /* Force text rebuild every frame when popup input is active (cursor blink) */
        if (ui.popup.visible && ui.popup_input_active)
            last_text_update = 0;

        /* Rebuild HUD text every second */
        {
            time_t now = time(NULL);
            if (now != last_text_update) {
                last_text_update = now;
                struct tm gt_buf, lt_buf;
                struct tm *gt = gmtime_r(&now, &gt_buf);
                struct tm *lt = localtime_r(&now, &lt_buf);
                if (!gt || !lt) continue;

                /* HUD: empty when sidebar is open, otherwise show info */
                if (sidebar_fb_w <= 0) {
                    char hud1[128], hud2[128];
                    snprintf(hud1, sizeof(hud1),
                             "Dist: %.1f km  Az to: %.1f^  Az from: %.1f^",
                             dist, az_to, az_from);
                    snprintf(hud2, sizeof(hud2),
                             "Local: %02d:%02d:%02d  UTC: %02d:%02d:%02d",
                             lt->tm_hour, lt->tm_min, lt->tm_sec,
                             gt->tm_hour, gt->tm_min, gt->tm_sec);
                    float size = 20.0f;
                    float hx1 = ((float)map_fb_w - text_width(hud1, size)) * 0.5f;
                    float hx2 = ((float)map_fb_w - text_width(hud2, size)) * 0.5f;
                    int vc = text_build(hud1, hx1, 16.0f, size, text_verts, 4096);
                    vc += text_build(hud2, hx2, 16.0f + size * 1.4f, size,
                                     text_verts + vc * 2, 4096 - vc);
                    renderer_upload_text(&renderer, text_verts, vc);
                } else {
                    renderer_upload_text(&renderer, text_verts, 0);
                }

                /* Sidebar: clocks + optional QRZ + distance/azimuth */
                if (sidebar_fb_w > 0) {
                    float sb_verts[4096];
                    float csz = 18.0f;
                    float margin = 16.0f;
                    float sbw = (float)sidebar_fb_w;
                    float y = margin;
                    int svc = 0;

                    char utc_line[64], loc_line[64];
                    snprintf(utc_line, sizeof(utc_line), "UTC  %02d:%02d:%02d",
                             gt->tm_hour, gt->tm_min, gt->tm_sec);
                    snprintf(loc_line, sizeof(loc_line), "LOC  %02d:%02d:%02d",
                             lt->tm_hour, lt->tm_min, lt->tm_sec);
                    svc += text_build(utc_line,
                                      (sbw - text_width(utc_line, csz)) * 0.5f, y,
                                      csz, sb_verts + svc * 2, 2048 - svc);
                    y += csz * 1.5f;
                    svc += text_build(loc_line,
                                      (sbw - text_width(loc_line, csz)) * 0.5f, y,
                                      csz, sb_verts + svc * 2, 2048 - svc);
                    y += csz * 2.5f;

                    /* Station info from swl dashboard */
                    if (ui.station_info_lines > 0) {
                        float sisz = 14.0f;
                        for (int si = 0; si < ui.station_info_lines; si++) {
                            svc += text_build(ui.station_info[si],
                                              (sbw - text_width(ui.station_info[si], sisz)) * 0.5f, y,
                                              sisz, sb_verts + svc * 2, 2048 - svc);
                            y += sisz * 1.5f;
                        }
                        y += csz * 1.0f;
                    }

                    if (dist > 0.0) {
                        char dist_line[64], azto_line[64], azfr_line[64];
                        snprintf(dist_line, sizeof(dist_line), "DIST  %.1f KM", dist);
                        snprintf(azto_line, sizeof(azto_line), "AZ TO  %.1f^", az_to);
                        snprintf(azfr_line, sizeof(azfr_line), "AZ FROM  %.1f^", az_from);
                        svc += text_build(dist_line,
                                          (sbw - text_width(dist_line, csz)) * 0.5f, y,
                                          csz, sb_verts + svc * 2, 2048 - svc);
                        y += csz * 1.5f;
                        svc += text_build(azto_line,
                                          (sbw - text_width(azto_line, csz)) * 0.5f, y,
                                          csz, sb_verts + svc * 2, 2048 - svc);
                        y += csz * 1.5f;
                        svc += text_build(azfr_line,
                                          (sbw - text_width(azfr_line, csz)) * 0.5f, y,
                                          csz, sb_verts + svc * 2, 2048 - svc);
                    }

                    renderer_upload_sidebar_text(&renderer, sb_verts, svc);
                }
            }
        }

        /* Update night overlay periodically */
        {
            time_t now = time(NULL);
            if (now - last_sun_update >= NIGHT_UPDATE_SEC) {
                last_sun_update = now;
                SubsolarPoint sun = solar_subsolar_point(now);
                nightmesh_build(&nightmesh, &sun);
                renderer_upload_night(&renderer, nightmesh.vertices,
                                      nightmesh.vertex_count);
            }
        }

        /* MUF / Aurora overlay: poll fetches and auto-refresh */
        {
            time_t now = time(NULL);

            /* Poll MUF fetch completion */
            if (muf_fetching) {
                int s = fetch_check(&muf_fetch);
                if (s != 0) {
                    muf_fetching = 0;
                    if (s == 1) {
                        char *json = fetch_take_response(&muf_fetch);
                        if (json) {
                            muf_data_free(&muf_data);
                            muf_data_init(&muf_data);
                            muf_parse_geojson(json, &muf_data);
                            free(json);
                            if (muf_active && muf_data.num_segments > 0)
                                renderer_upload_muf(&renderer, &muf_data);
                        }
                    }
                    fetch_cleanup(&muf_fetch);
                }
            }

            /* Poll aurora fetch completion */
            if (aurora_fetching) {
                int s = fetch_check(&aurora_fetch);
                if (s != 0) {
                    aurora_fetching = 0;
                    if (s == 1) {
                        char *json = fetch_take_response(&aurora_fetch);
                        if (json) {
                            aurora_parse_json(json, &aurora_grid);
                            free(json);
                            if (aurora_active && aurora_grid.valid) {
                                aurora_mesh_build(&aurora_mesh, &aurora_grid);
                                renderer_upload_aurora(&renderer, &aurora_mesh);
                            }
                        }
                    }
                    fetch_cleanup(&aurora_fetch);
                }
            }

            /* Auto-refresh every OVERLAY_UPDATE_SEC while active */
            if (muf_active && !muf_fetching &&
                now - last_muf_fetch >= OVERLAY_UPDATE_SEC) {
                fetch_start(&muf_fetch, MUF_URL);
                muf_fetching = 1;
                last_muf_fetch = now;
            }
            if (aurora_active && !aurora_fetching &&
                now - last_aurora_fetch >= OVERLAY_UPDATE_SEC) {
                fetch_start(&aurora_fetch, AURORA_URL);
                aurora_fetching = 1;
                last_aurora_fetch = now;
            }

            /* Poll Kp/Bz fetch completion */
            if (kp_fetching) {
                int s = fetch_check(&kp_fetch);
                if (s != 0) {
                    kp_fetching = 0;
                    if (s == 1) {
                        char *json = fetch_take_response(&kp_fetch);
                        if (json) {
                            geomag_parse_kp(json, &geomag);
                            free(json);
                        }
                    }
                    fetch_cleanup(&kp_fetch);
                }
            }
            if (bz_fetching) {
                int s = fetch_check(&bz_fetch);
                if (s != 0) {
                    bz_fetching = 0;
                    if (s == 1) {
                        char *json = fetch_take_response(&bz_fetch);
                        if (json) {
                            geomag_parse_bz(json, &geomag);
                            free(json);
                        }
                    }
                    fetch_cleanup(&bz_fetch);
                }
            }

            /* Auto-refresh Kp/Bz every OVERLAY_UPDATE_SEC while Aurora active */
            if (aurora_active && !kp_fetching && !bz_fetching &&
                now - last_geomag_fetch >= OVERLAY_UPDATE_SEC) {
                fetch_start(&kp_fetch, KP_URL);
                kp_fetching = 1;
                fetch_start(&bz_fetch, BZ_URL);
                bz_fetching = 1;
                last_geomag_fetch = now;
            }
        }

        renderer_draw(&renderer, mvp, map_fb_w, fb_h);

        /* Draw sidebar */
        if (sidebar_fb_w > 0) {
            glViewport(map_fb_w, 0, sidebar_fb_w, fb_h);
            renderer_upload_sidebar(&renderer, sidebar_fb_w, fb_h);
            renderer_draw_sidebar(&renderer, sidebar_fb_w, fb_h);
        }

        /* Draw buttons in full-window viewport (spans map + sidebar) */
        renderer_draw_buttons(&renderer, fb_w, fb_h);

        glfwSwapBuffers(window);
    }

    /* Save session state — use window (screen) size, not framebuffer size */
    int save_ww, save_wh;
    glfwGetWindowSize(window, &save_ww, &save_wh);
    config_save_state(target_lat, target_lon, target_name,
                      cam.zoom_km, cam.pan_x, cam.pan_y,
                      (int)projection_get_mode(),
                      input.center_lat, input.center_lon,
                      save_ww, save_wh, ui.sidebar_visible);

    /* Cleanup FIFO */
    if (fifo_fd >= 0) close(fifo_fd);
    unlink(FIFO_PATH);

    /* Cleanup */
    if (has_qrz) qrz_cleanup();
    renderer_destroy(&renderer);
    map_data_free(&map);
    if (has_borders) map_data_free(&borders);
    if (has_land) map_data_free(&land);
    free(grid.vertices);
    nightmesh_free(&nightmesh);
    muf_data_free(&muf_data);
    aurora_grid_free(&aurora_grid);
    aurora_mesh_free(&aurora_mesh);
    glfwDestroyWindow(window);
    glfwTerminate();
    return 0;
}
