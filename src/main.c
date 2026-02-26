#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <libgen.h>
#include <limits.h>
#include <unistd.h>
#include <math.h>
#include <GL/glew.h>
#include <GLFW/glfw3.h>

#include "projection.h"
#include "map_data.h"
#include "renderer.h"
#include "camera.h"
#include "input.h"
#include "text.h"
#include "grid.h"

#define DEFAULT_WIDTH  800
#define DEFAULT_HEIGHT 800
#define DEFAULT_SHP_REL "data/ne_110m_coastline/ne_110m_coastline.shp"
#define DEFAULT_BORDER_REL "data/ne_110m_admin_0_boundary_lines_land/ne_110m_admin_0_boundary_lines_land.shp"
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

static void print_usage(const char *prog)
{
    fprintf(stderr,
        "Usage: %s <center_lat> <center_lon> <target_lat> <target_lon> [options]\n"
        "\n"
        "  center_lat/lon  Center of azimuthal equidistant projection (degrees)\n"
        "  target_lat/lon  Second location to draw a line to (degrees)\n"
        "\n"
        "Options:\n"
        "  -c NAME    Center location name\n"
        "  -t NAME    Target location name\n"
        "  -s PATH    Shapefile path override (default: %s)\n"
        "\n"
        "Controls:\n"
        "  Scroll       Zoom in/out\n"
        "  Drag         Pan the map\n"
        "  Arrow keys   Pan the map\n"
        "  R            Reset view\n"
        "  Q / Esc      Quit\n",
        prog, DEFAULT_SHP_REL);
}

int main(int argc, char **argv)
{
    if (argc < 5) {
        print_usage(argv[0]);
        return 1;
    }

    double center_lat = atof(argv[1]);
    double center_lon = atof(argv[2]);
    double target_lat = atof(argv[3]);
    double target_lon = atof(argv[4]);

    const char *center_name = NULL;
    const char *target_name = NULL;
    const char *shp_override = NULL;

    /* Parse optional flags after the 4 positional args */
    int i = 5;
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

    char default_shp[PATH_MAX], default_border[PATH_MAX], shader_dir[PATH_MAX];
    resolve_path(exe_path, DEFAULT_SHP_REL, default_shp, sizeof(default_shp));
    resolve_path(exe_path, DEFAULT_BORDER_REL, default_border, sizeof(default_border));
    resolve_path(exe_path, DEFAULT_SHADER_REL, shader_dir, sizeof(shader_dir));

    const char *shp_path = shp_override ? shp_override : default_shp;

    /* Set up projection */
    projection_set_center(center_lat, center_lon);

    /* Project target point */
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

    /* Init GLFW */
    if (!glfwInit()) {
        fprintf(stderr, "Error: GLFW init failed\n");
        return 1;
    }

    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_SAMPLES, 4);

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

    /* Build grid (graticule) */
    MapData grid;
    grid.vertices = NULL;
    grid_build(&grid);

    /* Upload geometry to GPU */
    renderer_upload_map(&renderer, &map);
    if (has_borders)
        renderer_upload_borders(&renderer, &borders);
    renderer_upload_grid(&renderer, &grid);
    renderer_upload_target_line(&renderer, 0.0f, 0.0f, (float)tx, (float)ty);
    renderer_upload_earth_circle(&renderer);

    /* North pole marker */
    double npx, npy;
    projection_forward(90.0, 0.0, &npx, &npy);

    /* Build text overlay */
    text_init();
    char info_text[192];
    snprintf(info_text, sizeof(info_text), "Dist: %.1f km  Az to: %.1f^  Az from: %.1f^", dist, az_to, az_from);
    float text_verts[4096];
    int text_vcount = text_build(info_text, 16.0f, 16.0f, 20.0f, text_verts, 2048);
    renderer_upload_text(&renderer, text_verts, text_vcount);

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
    input_init(&input, window, &cam);

    /* Label vertex buffer (rebuilt each frame) */
    float label_verts[8192];

    /* Main loop */
    while (!glfwWindowShouldClose(window)) {
        glfwPollEvents();

        /* Update marker size relative to zoom */
        float ms = cam.zoom_km * 0.007f;
        renderer_upload_markers(&renderer, 0.0f, 0.0f, (float)tx, (float)ty, ms);
        renderer_upload_npole(&renderer, (float)npx, (float)npy, ms);

        glfwGetFramebufferSize(window, &fb_w, &fb_h);

        float mvp[16];
        camera_get_mvp(&cam, mvp);

        /* Build labels at screen positions of center and target markers */
        float label_size = 14.0f;
        float cpx, cpy, tpx, tpy;
        km_to_pixel(mvp, 0.0f, 0.0f, fb_w, fb_h, &cpx, &cpy);
        km_to_pixel(mvp, (float)tx, (float)ty, fb_w, fb_h, &tpx, &tpy);

        /* Center label: offset below center crosshair */
        int center_vcount = text_build(center_label,
            cpx - (float)strlen(center_label) * label_size * 0.35f * 0.5f,
            cpy + label_size * 0.8f,
            label_size, label_verts, 4096);
        /* Target label: offset below target crosshair */
        int target_vcount = text_build(target_label,
            tpx - (float)strlen(target_label) * label_size * 0.35f * 0.5f,
            tpy + label_size * 0.8f,
            label_size, label_verts + center_vcount * 2, 4096 - center_vcount);

        renderer_upload_labels(&renderer, label_verts, center_vcount + target_vcount, center_vcount);

        renderer_draw(&renderer, mvp, fb_w, fb_h);

        glfwSwapBuffers(window);
    }

    /* Cleanup */
    renderer_destroy(&renderer);
    map_data_free(&map);
    if (has_borders) map_data_free(&borders);
    free(grid.vertices);
    glfwDestroyWindow(window);
    glfwTerminate();
    return 0;
}
