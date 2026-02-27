#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <libgen.h>
#include <limits.h>
#include <unistd.h>
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

#define DEFAULT_WIDTH  800
#define DEFAULT_HEIGHT 800
#define DEFAULT_SHP_REL "data/ne_110m_coastline/ne_110m_coastline.shp"
#define DEFAULT_BORDER_REL "data/ne_110m_admin_0_boundary_lines_land/ne_110m_admin_0_boundary_lines_land.shp"
#define DEFAULT_LAND_REL "data/ne_110m_land/ne_110m_land.shp"
#define DEFAULT_SHADER_REL "shaders"

/* Resolve a path relative to the executable's directory. */
static void resolve_path(const char *exe, const char *rel, char *out, size_t out_size)
{
    char exe_copy[PATH_MAX];
    strncpy(exe_copy, exe, PATH_MAX - 1);
    exe_copy[PATH_MAX - 1] = '\0';
    char *dir = dirname(exe_copy);
    snprintf(out, out_size, "%s/../%s", dir, rel);
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

static void print_usage(const char *prog)
{
    fprintf(stderr,
        "Usage: %s <center_lat> <center_lon> <target_lat> <target_lon> [options]\n"
        "       %s <target_lat> <target_lon> [options]  (center from config)\n"
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
        prog, prog, DEFAULT_SHP_REL);
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

    if (npos >= 4) {
        /* Full mode: center + target from CLI */
        center_lat = atof(argv[1]);
        center_lon = atof(argv[2]);
        target_lat = atof(argv[3]);
        target_lon = atof(argv[4]);
        opt_start = 5;
    } else if (npos >= 2 && has_config) {
        /* Config mode: center from config, target from CLI */
        center_lat = cfg.lat;
        center_lon = cfg.lon;
        if (cfg.name[0])
            center_name = cfg.name;
        target_lat = atof(argv[1]);
        target_lon = atof(argv[2]);
        opt_start = 3;
    } else {
        if (npos >= 2 && !has_config)
            fprintf(stderr, "Error: 2 args given but no valid config file found.\n"
                            "Create ~/.config/azmap.conf with lat and lon, or pass 4 positional args.\n\n");
        print_usage(argv[0]);
        return 1;
    }

    /* Parse optional flags */
    int i = opt_start;
    while (i < argc) {
        if (strcmp(argv[i], "-c") == 0 && i + 1 < argc) {
            center_name = argv[++i];
        } else if (strcmp(argv[i], "-t") == 0 && i + 1 < argc) {
            target_name = argv[++i];
        } else if (strcmp(argv[i], "-s") == 0 && i + 1 < argc) {
            shp_override = argv[++i];
        } else if (argv[i][0] != '-' && !shp_override) {
            /* Backward compat: bare arg = shapefile path */
            shp_override = argv[i];
        } else {
            fprintf(stderr, "Unknown option: %s\n", argv[i]);
            print_usage(argv[0]);
            return 1;
        }
        i++;
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

    GLFWwindow *window = glfwCreateWindow(DEFAULT_WIDTH, DEFAULT_HEIGHT, "azMap", NULL, NULL);
    if (!window) {
        fprintf(stderr, "Error: window creation failed\n");
        glfwTerminate();
        return 1;
    }
    glfwMakeContextCurrent(window);
    glfwSwapInterval(1);

    /* Init GLEW — ignore GLEW_ERROR_NO_GLX_DISPLAY on Wayland */
    glewExperimental = GL_TRUE;
    GLenum glew_err = glewInit();
    if (glew_err != GLEW_OK && glew_err != 4 /* GLEW_ERROR_NO_GLX_DISPLAY */) {
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

    /* Build grid (graticule) */
    MapData grid;
    grid.vertices = NULL;
    grid_build(&grid);

    /* Night overlay */
    NightMesh nightmesh;
    nightmesh_init(&nightmesh);

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
    int btn_home   = ui_add_button(&ui, "Home",   0, 0, 90, 30);
    int btn_proj   = ui_add_button(&ui, "Proj",   0, 0, 90, 30);
    int btn_mode   = ui_add_button(&ui, "Mode",   0, 0, 90, 30);
    int btn_layers = ui_add_button(&ui, "Layers", 0, 0, 90, 30);
    /* Mode sub-buttons */
    int btn_opt1 = ui_add_button(&ui, "QRZ",  0, 0, 90, 30);
    int btn_opt2 = ui_add_button(&ui, "WSJT", 0, 0, 90, 30);
    int btn_opt3 = ui_add_button(&ui, "BCB",  0, 0, 90, 30);
    /* Layers sub-buttons */
    int btn_aurora = ui_add_button(&ui, "Aurora",  0, 0, 110, 30);
    int btn_spore  = ui_add_button(&ui, "Spor. E", 0, 0, 110, 30);
    int btn_muf    = ui_add_button(&ui, "MUF",     0, 0, 110, 30);
    /* Start in home mode */
    ui.buttons[btn_opt1].visible = 0;
    ui.buttons[btn_opt2].visible = 0;
    ui.buttons[btn_opt3].visible = 0;
    ui.buttons[btn_aurora].visible = 0;
    ui.buttons[btn_spore].visible = 0;
    ui.buttons[btn_muf].visible = 0;

    /* Camera — use actual framebuffer size (differs from window size on HiDPI) */
    Camera cam;
    camera_init(&cam);

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

    /* Main loop */
    while (!glfwWindowShouldClose(window)) {
        glfwPollEvents();

        /* Handle projection center change (drag / arrow keys) */
        if (input.center_dirty) {
            input.center_dirty = 0;
            projection_set_center(input.center_lat, input.center_lon);
            map_data_reproject(&map, NULL);
            renderer_upload_map(&renderer, &map);
            if (has_borders) {
                map_data_reproject(&borders, NULL);
                renderer_upload_borders(&renderer, &borders);
            }
            if (has_land) {
                map_data_reproject_nosplit(&land);
                renderer_upload_land(&renderer, &land);
            }
            projection_forward(center_lat, center_lon, &cx, &cy);
            projection_forward(target_lat, target_lon, &tx, &ty);
            {
                float gc_verts[GC_LINE_POINTS * 2];
                int gc_n = build_gc_line(center_lat, center_lon, target_lat, target_lon, gc_verts);
                renderer_upload_target_line(&renderer, gc_verts, gc_n);
            }
            projection_forward(90.0, 0.0, &npx, &npy);
            /* In ortho mode, grid depends on projection center */
            if (projection_get_mode() == PROJ_ORTHO) {
                grid_build_geo(&grid);
                renderer_upload_grid(&renderer, &grid);
            }
            last_sun_update = 0; /* force night mesh rebuild */
        }

        /* Update marker size relative to zoom */
        float ms = cam.zoom_km * 0.005f;
        renderer_upload_markers(&renderer, (float)cx, (float)cy, (float)tx, (float)ty, ms);
        renderer_upload_npole(&renderer, (float)npx, (float)npy, ms);

        glfwGetFramebufferSize(window, &fb_w, &fb_h);

        float mvp[16];
        camera_get_mvp(&cam, mvp);

        /* Build labels at screen positions of center and target markers */
        float label_size = 14.0f;
        float cpx, cpy, tpx, tpy;
        km_to_pixel(mvp, (float)cx, (float)cy, fb_w, fb_h, &cpx, &cpy);
        km_to_pixel(mvp, (float)tx, (float)ty, fb_w, fb_h, &tpx, &tpy);

        /* Center label: offset above center marker */
        float cw = text_width(center_label, label_size);
        float clx = cpx - cw * 0.5f;
        float cly = cpy - label_size * 1.8f;
        int center_vcount = text_build(center_label, clx, cly,
            label_size, label_verts, 4096);
        /* Target label: offset below target crosshair */
        float tw = text_width(target_label, label_size);
        float tlx = tpx - tw * 0.5f;
        float tly = tpy + label_size * 0.8f;
        int target_vcount = text_build(target_label, tlx, tly,
            label_size, label_verts + center_vcount * 2, 4096 - center_vcount);

        renderer_upload_labels(&renderer, label_verts, center_vcount + target_vcount, center_vcount);

        /* Label backgrounds */
        float bg_verts[24]; /* 2 quads * 6 verts * 2 floats */
        float pad = 4.0f;
        int cbg = build_label_bg(clx, cly, cw, label_size, pad, bg_verts);
        int tbg = build_label_bg(tlx, tly, tw, label_size, pad, bg_verts + cbg * 2);
        renderer_upload_label_bgs(&renderer, bg_verts, cbg + tbg, cbg);

        /* Update button positions (horizontal, centered at bottom) */
        {
            float bw = 90.0f, bh = 30.0f, gap = 10.0f, margin = 10.0f;
            int n = ui.count;
            float total_w = n * bw + (n - 1) * gap;
            float start_x = ((float)fb_w - total_w) * 0.5f;
            float by = (float)fb_h - bh - margin;
            for (int bi = 0; bi < n; bi++) {
                ui.buttons[bi].x = start_x + bi * (bw + gap);
                ui.buttons[bi].y = by;
                ui.buttons[bi].w = bw;
                ui.buttons[bi].h = bh;
            }
        }

        /* Build and upload button geometry */
        {
            float btn_quads[UI_MAX_BUTTONS * 12];
            float btn_text[8192];
            int quad_count, text_count, hovered_quad;
            ui_build_geometry(&ui, btn_quads, &quad_count,
                              btn_text, &text_count, &hovered_quad);
            if (quad_count > 0 || text_count > 0)
                renderer_upload_buttons(&renderer, btn_quads, quad_count,
                                        btn_text, text_count,
                                        quad_count / 6, hovered_quad);
        }

        /* Build and upload popup geometry */
        if (ui.popup.visible) {
            float popup_quads[4 * 12]; /* 4 quads * 6 verts * 2 floats */
            float popup_text[4096];
            int pq_count, pt_count;
            ui_build_popup_geometry(&ui, fb_w, fb_h,
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
                /* Reproject all map geometry */
                map_data_reproject(&map, NULL);
                renderer_upload_map(&renderer, &map);
                if (has_borders) {
                    map_data_reproject(&borders, NULL);
                    renderer_upload_borders(&renderer, &borders);
                }
                if (has_land) {
                    map_data_reproject_nosplit(&land);
                    renderer_upload_land(&renderer, &land);
                }
                /* Re-project key points */
                projection_forward(center_lat, center_lon, &cx, &cy);
                projection_forward(target_lat, target_lon, &tx, &ty);
                {
                    float gc_verts[GC_LINE_POINTS * 2];
                    int gc_n = build_gc_line(center_lat, center_lon, target_lat, target_lon, gc_verts);
                    renderer_upload_target_line(&renderer, gc_verts, gc_n);
                }
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
                /* Clamp zoom */
                double max_diam = 2.0 * projection_get_radius();
                if (cam.zoom_km > (float)max_diam)
                    cam.zoom_km = (float)max_diam;
            } else if (ui.clicked == btn_mode) {
                /* Show mode sub-buttons, hide top-level menus */
                ui.buttons[btn_proj].visible = 0;
                ui.buttons[btn_mode].visible = 0;
                ui.buttons[btn_layers].visible = 0;
                ui.buttons[btn_opt1].visible = 1;
                ui.buttons[btn_opt2].visible = 1;
                ui.buttons[btn_opt3].visible = 1;
            } else if (ui.clicked == btn_layers) {
                /* Show layers sub-buttons, hide top-level menus */
                ui.buttons[btn_proj].visible = 0;
                ui.buttons[btn_mode].visible = 0;
                ui.buttons[btn_layers].visible = 0;
                ui.buttons[btn_aurora].visible = 1;
                ui.buttons[btn_spore].visible = 1;
                ui.buttons[btn_muf].visible = 1;
            } else if (ui.clicked == btn_opt1) {
                ui_show_popup(&ui, "QRZ LOOKUP");
                if (!has_qrz) {
                    strncpy(ui.popup_result[0], "NO QRZ CREDENTIALS", sizeof(ui.popup_result[0]) - 1);
                    strncpy(ui.popup_result[1], "IN CONFIG", sizeof(ui.popup_result[1]) - 1);
                    ui.popup_result_lines = 2;
                    ui.popup_input_active = 0;
                }
            } else if (ui.clicked == btn_opt2) {
                ui_show_popup(&ui, "WSJT");
            } else if (ui.clicked == btn_opt3) {
                ui_show_popup(&ui, "BCB");
            } else if (ui.clicked == btn_home) {
                /* Collapse everything back to home */
                ui.buttons[btn_proj].visible = 1;
                ui.buttons[btn_mode].visible = 1;
                ui.buttons[btn_layers].visible = 1;
                ui.buttons[btn_opt1].visible = 0;
                ui.buttons[btn_opt2].visible = 0;
                ui.buttons[btn_opt3].visible = 0;
                ui.buttons[btn_aurora].visible = 0;
                ui.buttons[btn_spore].visible = 0;
                ui.buttons[btn_muf].visible = 0;
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
                /* Recompute distance/azimuth from current projection center */
                dist = projection_distance(input.center_lat, input.center_lon,
                                           target_lat, target_lon);
                az_to = projection_azimuth(input.center_lat, input.center_lon,
                                           target_lat, target_lon);
                az_from = projection_azimuth(target_lat, target_lon,
                                             input.center_lat, input.center_lon);
                /* Re-project target */
                projection_forward(target_lat, target_lon, &tx, &ty);
                projection_forward(center_lat, center_lon, &cx, &cy);
                {
                    float gc_verts[GC_LINE_POINTS * 2];
                    int gc_n = build_gc_line(center_lat, center_lon, target_lat, target_lon, gc_verts);
                    renderer_upload_target_line(&renderer, gc_verts, gc_n);
                }
                /* Force HUD rebuild */
                last_text_update = 0;

                /* Fill popup result lines */
                strncpy(ui.popup_result[0], qrz_result.call,
                        sizeof(ui.popup_result[0]) - 1);
                /* Uppercase name for stroke font */
                {
                    char upper_name[128];
                    int ni = 0;
                    for (int ci = 0; qrz_result.name[ci] && ni < 127; ci++)
                        upper_name[ni++] = (char)toupper((unsigned char)qrz_result.name[ci]);
                    upper_name[ni] = '\0';
                    strncpy(ui.popup_result[1], upper_name,
                            sizeof(ui.popup_result[1]) - 1);
                }
                {
                    char upper_loc[128];
                    int li = 0;
                    for (int ci = 0; qrz_result.location[ci] && li < 127; ci++)
                        upper_loc[li++] = (char)toupper((unsigned char)qrz_result.location[ci]);
                    upper_loc[li] = '\0';
                    strncpy(ui.popup_result[2], upper_loc,
                            sizeof(ui.popup_result[2]) - 1);
                }
                {
                    char coord[64];
                    format_coord(coord, sizeof(coord), qrz_result.lat, qrz_result.lon);
                    char upper_grid[16];
                    int gi = 0;
                    for (int ci = 0; qrz_result.grid[ci] && gi < 15; ci++)
                        upper_grid[gi++] = (char)toupper((unsigned char)qrz_result.grid[ci]);
                    upper_grid[gi] = '\0';
                    snprintf(ui.popup_result[3], sizeof(ui.popup_result[3]),
                             "GRID: %.10s  %.24s", upper_grid, coord);
                }
                ui.popup_result_lines = 4;
                ui.popup_input_active = 0;
            } else {
                /* Error */
                char upper_err[64];
                int ei = 0;
                for (int ci = 0; err_buf[ci] && ei < 63; ci++)
                    upper_err[ei++] = (char)toupper((unsigned char)err_buf[ci]);
                upper_err[ei] = '\0';
                strncpy(ui.popup_result[0], upper_err,
                        sizeof(ui.popup_result[0]) - 1);
                ui.popup_result_lines = 1;
            }
        }

        /* Rebuild HUD text every second (includes live clock) */
        {
            time_t now = time(NULL);
            if (now != last_text_update) {
                last_text_update = now;
                struct tm *gt = gmtime(&now);
                struct tm *lt = localtime(&now);
                if (!gt || !lt) continue;
                char line1[128], line2[128];
                snprintf(line1, sizeof(line1),
                         "Dist: %.1f km  Az to: %.1f^  Az from: %.1f^",
                         dist, az_to, az_from);
                snprintf(line2, sizeof(line2),
                         "Local: %02d:%02d:%02d  UTC: %02d:%02d:%02d",
                         lt->tm_hour, lt->tm_min, lt->tm_sec,
                         gt->tm_hour, gt->tm_min, gt->tm_sec);
                float size = 20.0f;
                float x1 = ((float)fb_w - text_width(line1, size)) * 0.5f;
                float x2 = ((float)fb_w - text_width(line2, size)) * 0.5f;
                int vc = text_build(line1, x1, 16.0f, size, text_verts, 4096);
                vc += text_build(line2, x2, 16.0f + size * 1.4f, size,
                                 text_verts + vc * 2, 4096 - vc);
                renderer_upload_text(&renderer, text_verts, vc);
            }
        }

        /* Update night overlay periodically */
        {
            time_t now = time(NULL);
            if (now - last_sun_update >= 60) {
                last_sun_update = now;
                SubsolarPoint sun = solar_subsolar_point(now);
                nightmesh_build(&nightmesh, &sun);
                renderer_upload_night(&renderer, nightmesh.vertices,
                                      nightmesh.vertex_count);
            }
        }

        renderer_draw(&renderer, mvp, fb_w, fb_h);

        glfwSwapBuffers(window);
    }

    /* Cleanup */
    if (has_qrz) qrz_cleanup();
    renderer_destroy(&renderer);
    map_data_free(&map);
    if (has_borders) map_data_free(&borders);
    if (has_land) map_data_free(&land);
    free(grid.vertices);
    nightmesh_free(&nightmesh);
    glfwDestroyWindow(window);
    glfwTerminate();
    return 0;
}
