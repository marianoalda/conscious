/*
 * Perspective view of a Conscious world file.
 *
 * Coordinates follow UTM: X grows east, Y grows north, Z is elevation.
 * The southwest corner of the world is the origin.
 *
 * Each stored heightmap cell is one sample. The drawn vertex at a grid
 * corner is the average of the cells that meet there, so a cell becomes
 * a quad that covers its own footprint and can tilt. The quad is split
 * by the diagonal from its southwest corner to its northeast corner.
 *
 * Daylight is the world's diffuse-light cell divided by its maximum.
 * It brightens the scene a little. Directional shade uses the DIRECT
 * weight on every face, including at night. DIFFUSE is the unlit floor.
 * L points toward the northeast, above the horizon.
 *
 * Terrain is brown. Grass is green on that same face. Static water is
 * cyan raised on the terrain. Tab cycles the underside sheet among
 * humidity (blue), fertility (amber), and grass age (magenta): each is
 * hung from the heightmap zero and grows downward, visible from below.
 */

#include "world.h"

#include <GL/gl.h>
#include <GL/glu.h>
#include <GL/glx.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/keysym.h>

#include <math.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define DEFAULT_DIFFUSE 0.25f
#define DEFAULT_DIRECT 0.85f
#define LIGHT_STEP 0.05f
#define LIGHT_MIN 0.0f
#define LIGHT_MAX 2.0f

typedef enum {
    UNDERSHEET_HUMIDITY = 0,
    UNDERSHEET_FERTILITY,
    UNDERSHEET_GRASSAGE,
    UNDERSHEET_COUNT
} undersheet_mode_t;

typedef struct {
    float x;
    float y;
    float z;
} vec3;

typedef struct {
    uint32_t cell_width;
    uint32_t cell_depth;
    uint32_t corners_x;
    uint32_t corners_y;
    vec3 *corners;
    float *cell_water_m;
    float *corner_water_m;
    float *corner_fertility_m;
    float *corner_grassage_m;
    float *cell_grass_alpha;
    float *corner_humidity_m;
    float datum_m;
    float sheet_reach_m; /* greatest elevation; saturated underside depth */
    float span_m;
} mesh_t;

typedef struct {
    float target_x;
    float target_y;
    float target_z;
    float distance;
    float azimuth;
    float elevation;
    float diffuse;
    float daylight;
    float direct;
    float pan_step;
} view_t;

static mesh_t mesh;
static view_t view;
static Display *display;
static Window window;
static int window_width = 960;
static int window_height = 720;
static bool dragging = false;
static int last_x;
static int last_y;
static bool dirty = true;
static undersheet_mode_t undersheet_mode = UNDERSHEET_HUMIDITY;

static const float light_x = 0.45f;
static const float light_y = 0.25f;
static const float light_z = 0.86f;
static const float ground_red = 0.50f;
static const float ground_green = 0.31f;
static const float ground_blue = 0.16f;
static const float water_red = 0.106f;
static const float water_green = 0.399f;
static const float water_blue = 0.436f;
static const float fertility_red = 0.55f;
static const float fertility_green = 0.32f;
static const float fertility_blue = 0.08f;
static const float grassage_red = 0.52f;
static const float grassage_green = 0.18f;
static const float grassage_blue = 0.48f;
static const float grass_red = 0.18f;
static const float grass_green = 0.48f;
static const float grass_blue = 0.12f;
static const float humidity_red = 0.12f;
static const float humidity_green = 0.22f;
static const float humidity_blue = 0.72f;

static void die(const char *message)
{
    fprintf(stderr, "world-view: %s\n", message);
    exit(EXIT_FAILURE);
}

static void usage(void)
{
    fprintf(
        stderr,
        "Usage: world-view WORLD [--diffuse VALUE] [--direct VALUE]\n"
        "\n"
        "WORLD       Conscious world file (required)\n"
        "--diffuse   unlit floor light, default %.2f (0..2)\n"
        "--direct    slope lighting weight, default %.2f (0..2)\n"
        "\n"
        "Mouse wheel zooms. Drag with the left button to orbit,\n"
        "above or below the world.\n"
        "Arrow keys pan. Tab cycles humidity / fertility / grass age\n"
        "undersheets (view from below the world). , . adjust diffuse.\n"
        "[ ] adjust direct. Esc or q quits.\n"
        "While a frame is drawn, queued input is drained so the\n"
        "view stays fluid instead of replaying a backlog of redraws.\n",
        DEFAULT_DIFFUSE,
        DEFAULT_DIRECT);
}

static float clampf(float value, float low, float high)
{
    if (value < low) {
        return low;
    }
    if (value > high) {
        return high;
    }
    return value;
}

static float parse_light(const char *text, const char *name)
{
    char *end;
    float value;

    value = strtof(text, &end);
    if (end == text || *end != '\0' || !isfinite(value) || value < 0.0f) {
        fprintf(stderr, "world-view: %s must be a number >= 0\n", name);
        usage();
        exit(EXIT_FAILURE);
    }
    return value;
}

static void print_lights(void)
{
    fprintf(
        stderr,
        "world-view: diffuse=%.2f direct=%.2f\n",
        view.diffuse,
        view.direct);
}

static const char *undersheet_name(undersheet_mode_t mode)
{
    switch (mode) {
    case UNDERSHEET_HUMIDITY:
        return "humidity";
    case UNDERSHEET_FERTILITY:
        return "fertility";
    case UNDERSHEET_GRASSAGE:
        return "grass age";
    default:
        return "unknown";
    }
}

static const world_layer_t *find_layer(
    const world_state_t *world,
    world_layer_type_t type)
{
    uint32_t i;

    for (i = 0; i < world->layer_count; i++) {
        const world_layer_t *layer = world->layers[i];

        if (layer != NULL && layer->type == type && layer->payload != NULL) {
            return layer;
        }
    }
    return NULL;
}

static const world_heightmap_payload_t *find_heightmap(
    const world_state_t *world)
{
    const world_layer_t *layer = find_layer(world, WORLD_LAYER_HEIGHTMAP);

    return layer != NULL ? layer->payload : NULL;
}

static const world_u16_payload_t *find_humidity(const world_state_t *world)
{
    const world_layer_t *layer = find_layer(world, WORLD_LAYER_HUMIDITY);

    return layer != NULL ? layer->payload : NULL;
}

static const world_u8_payload_t *find_u8(
    const world_state_t *world,
    world_layer_type_t type)
{
    const world_layer_t *layer = find_layer(world, type);

    return layer != NULL ? layer->payload : NULL;
}

static const world_staticwater_payload_t *find_staticwater(
    const world_state_t *world)
{
    const world_layer_t *layer = find_layer(world, WORLD_LAYER_STATICWATER);

    return layer != NULL ? layer->payload : NULL;
}

/* Daylight fraction from the published irradiance. A missing layer is night. */
static float file_daylight(const world_state_t *world)
{
    const world_layer_t *layer = find_layer(world, WORLD_LAYER_DIFFLIGHT);
    const world_difflight_payload_t *light;
    uint64_t cell_count;
    uint64_t cell;
    double sum;

    if (layer == NULL) {
        return 0.0f;
    }
    light = layer->payload;
    if (light == NULL ||
        light->published == NULL ||
        light->max_irradiance == 0 ||
        light->cell_size == 0 ||
        world->width % light->cell_size != 0 ||
        world->depth % light->cell_size != 0) {
        return 0.0f;
    }

    cell_count =
        (uint64_t)(world->width / light->cell_size) *
        (uint64_t)(world->depth / light->cell_size);
    if (cell_count == 0) {
        return 0.0f;
    }

    sum = 0.0;
    for (cell = 0; cell < cell_count; cell++) {
        sum += (double)light->published[cell];
    }

    return (float)((sum / (double)cell_count) / (double)light->max_irradiance);
}

static double cell_elevation_m(
    const world_heightmap_payload_t *heightmap,
    uint32_t cell_width,
    uint32_t column,
    uint32_t row)
{
    uint32_t offset = heightmap->published[row * cell_width + column];

    return ((double)heightmap->min_height + (double)offset) / 1000.0;
}

static void free_mesh(void)
{
    free(mesh.cell_water_m);
    free(mesh.corner_water_m);
    free(mesh.corner_fertility_m);
    free(mesh.corner_grassage_m);
    free(mesh.cell_grass_alpha);
    free(mesh.corner_humidity_m);
    free(mesh.corners);
    memset(&mesh, 0, sizeof(mesh));
}

static void average_corners_from_cells(
    const float *cell_values,
    float *corner_values)
{
    uint32_t column;
    uint32_t row;

    for (row = 0; row < mesh.corners_y; row++) {
        for (column = 0; column < mesh.corners_x; column++) {
            double sum = 0.0;
            int count = 0;
            int d_row;
            int d_column;

            for (d_row = -1; d_row <= 0; d_row++) {
                for (d_column = -1; d_column <= 0; d_column++) {
                    int cell_column = (int)column + d_column;
                    int cell_row = (int)row + d_row;

                    if (cell_column < 0 || cell_row < 0 ||
                        cell_column >= (int)mesh.cell_width ||
                        cell_row >= (int)mesh.cell_depth) {
                        continue;
                    }
                    sum += cell_values[
                        (uint32_t)cell_row * mesh.cell_width +
                        (uint32_t)cell_column];
                    count++;
                }
            }
            if (count > 0) {
                corner_values[row * mesh.corners_x + column] =
                    (float)(sum / count);
            }
        }
    }
}

/* Depth below heightmap zero: (value / 255) × sheet_reach_m. */
static void fill_u8_undersheet(
    const world_state_t *world,
    const world_heightmap_payload_t *heightmap,
    const world_u8_payload_t *layer,
    float *corner_out)
{
    uint32_t column;
    uint32_t row;
    uint32_t layer_columns;
    uint32_t layer_rows;
    float *cell_depth;

    if (layer == NULL ||
        layer->published == NULL ||
        layer->cell_size == 0 ||
        mesh.sheet_reach_m <= 0.0f ||
        world->width % layer->cell_size != 0 ||
        world->depth % layer->cell_size != 0) {
        return;
    }

    cell_depth = calloc(
        (size_t)mesh.cell_width * mesh.cell_depth,
        sizeof(*cell_depth));
    if (cell_depth == NULL) {
        return;
    }

    layer_columns = world->width / layer->cell_size;
    layer_rows = world->depth / layer->cell_size;

    for (row = 0; row < mesh.cell_depth; row++) {
        for (column = 0; column < mesh.cell_width; column++) {
            uint32_t east_mm =
                column * heightmap->cell_size + heightmap->cell_size / 2;
            uint32_t north_mm =
                row * heightmap->cell_size + heightmap->cell_size / 2;
            uint32_t layer_column = east_mm / layer->cell_size;
            uint32_t layer_row = north_mm / layer->cell_size;
            uint8_t value;

            if (layer_column >= layer_columns) {
                layer_column = layer_columns - 1;
            }
            if (layer_row >= layer_rows) {
                layer_row = layer_rows - 1;
            }
            value = layer->published[layer_row * layer_columns + layer_column];
            cell_depth[row * mesh.cell_width + column] =
                ((float)value / 255.0f) * mesh.sheet_reach_m;
        }
    }
    average_corners_from_cells(cell_depth, corner_out);
    free(cell_depth);
}

static int build_mesh(const world_state_t *world)
{
    const world_heightmap_payload_t *heightmap;
    uint32_t column;
    uint32_t row;
    double cell_m;
    size_t cell_count;
    size_t corner_count;

    heightmap = find_heightmap(world);
    if (heightmap == NULL || heightmap->published == NULL ||
        heightmap->cell_size == 0) {
        return -1;
    }
    if (world->width % heightmap->cell_size != 0 ||
        world->depth % heightmap->cell_size != 0) {
        return -1;
    }

    mesh.cell_width = world->width / heightmap->cell_size;
    mesh.cell_depth = world->depth / heightmap->cell_size;
    if (mesh.cell_width == 0 || mesh.cell_depth == 0) {
        return -1;
    }

    mesh.corners_x = mesh.cell_width + 1;
    mesh.corners_y = mesh.cell_depth + 1;
    cell_count = (size_t)mesh.cell_width * mesh.cell_depth;
    corner_count = (size_t)mesh.corners_x * mesh.corners_y;

    mesh.corners = calloc(corner_count, sizeof(*mesh.corners));
    mesh.cell_water_m = calloc(cell_count, sizeof(*mesh.cell_water_m));
    mesh.corner_water_m = calloc(corner_count, sizeof(*mesh.corner_water_m));
    mesh.corner_fertility_m =
        calloc(corner_count, sizeof(*mesh.corner_fertility_m));
    mesh.corner_grassage_m =
        calloc(corner_count, sizeof(*mesh.corner_grassage_m));
    mesh.cell_grass_alpha = calloc(cell_count, sizeof(*mesh.cell_grass_alpha));
    mesh.corner_humidity_m =
        calloc(corner_count, sizeof(*mesh.corner_humidity_m));
    if (mesh.corners == NULL ||
        mesh.cell_water_m == NULL ||
        mesh.corner_water_m == NULL ||
        mesh.corner_fertility_m == NULL ||
        mesh.corner_grassage_m == NULL ||
        mesh.cell_grass_alpha == NULL ||
        mesh.corner_humidity_m == NULL) {
        free_mesh();
        return -1;
    }

    cell_m = (double)heightmap->cell_size / 1000.0;
    mesh.span_m = (float)fmax(
        (double)world->width / 1000.0,
        (double)world->depth / 1000.0);
    mesh.datum_m = (float)((double)heightmap->min_height / 1000.0);
    mesh.sheet_reach_m = mesh.datum_m;

    for (row = 0; row < mesh.corners_y; row++) {
        for (column = 0; column < mesh.corners_x; column++) {
            vec3 *corner = &mesh.corners[row * mesh.corners_x + column];
            double sum = 0.0;
            int count = 0;
            int d_row;
            int d_column;

            corner->x = (float)(column * cell_m);
            corner->y = (float)(row * cell_m);

            for (d_row = -1; d_row <= 0; d_row++) {
                for (d_column = -1; d_column <= 0; d_column++) {
                    int cell_column = (int)column + d_column;
                    int cell_row = (int)row + d_row;

                    if (cell_column < 0 || cell_row < 0 ||
                        cell_column >= (int)mesh.cell_width ||
                        cell_row >= (int)mesh.cell_depth) {
                        continue;
                    }
                    double elevation = cell_elevation_m(
                        heightmap,
                        mesh.cell_width,
                        (uint32_t)cell_column,
                        (uint32_t)cell_row);

                    sum += elevation;
                    if ((float)elevation > mesh.sheet_reach_m) {
                        mesh.sheet_reach_m = (float)elevation;
                    }
                    count++;
                }
            }
            corner->z = (float)(sum / count);
        }
    }

    if (mesh.sheet_reach_m < 0.0f) {
        mesh.sheet_reach_m = 0.0f;
    }

    {
        const world_staticwater_payload_t *water = find_staticwater(world);

        if (water != NULL &&
            water->published != NULL &&
            water->cell_size == heightmap->cell_size) {
            for (row = 0; row < mesh.cell_depth; row++) {
                for (column = 0; column < mesh.cell_width; column++) {
                    uint32_t index = row * mesh.cell_width + column;
                    double depth_mm =
                        (double)water->min_depth +
                        (double)water->published[index];

                    if (depth_mm < 0.0) {
                        depth_mm = 0.0;
                    }
                    mesh.cell_water_m[index] = (float)(depth_mm / 1000.0);
                }
            }
            average_corners_from_cells(mesh.cell_water_m, mesh.corner_water_m);
        }
    }

    fill_u8_undersheet(
        world,
        heightmap,
        find_u8(world, WORLD_LAYER_FERTILITY),
        mesh.corner_fertility_m);
    fill_u8_undersheet(
        world,
        heightmap,
        find_u8(world, WORLD_LAYER_GRASSAGE),
        mesh.corner_grassage_m);

    {
        const world_u8_payload_t *grass = find_u8(world, WORLD_LAYER_GRASS);

        if (grass != NULL &&
            grass->published != NULL &&
            grass->cell_size != 0 &&
            world->width % grass->cell_size == 0 &&
            world->depth % grass->cell_size == 0) {
            uint32_t grass_columns = world->width / grass->cell_size;
            uint32_t grass_rows = world->depth / grass->cell_size;

            for (row = 0; row < mesh.cell_depth; row++) {
                for (column = 0; column < mesh.cell_width; column++) {
                    uint32_t east_mm =
                        column * heightmap->cell_size +
                        heightmap->cell_size / 2;
                    uint32_t north_mm =
                        row * heightmap->cell_size +
                        heightmap->cell_size / 2;
                    uint32_t grass_column = east_mm / grass->cell_size;
                    uint32_t grass_row = north_mm / grass->cell_size;
                    uint8_t height;

                    if (grass_column >= grass_columns) {
                        grass_column = grass_columns - 1;
                    }
                    if (grass_row >= grass_rows) {
                        grass_row = grass_rows - 1;
                    }
                    height = grass->published[
                        grass_row * grass_columns + grass_column];
                    mesh.cell_grass_alpha[row * mesh.cell_width + column] =
                        (float)height / 255.0f;
                }
            }
        }
    }

    {
        const world_u16_payload_t *humidity = find_humidity(world);

        if (humidity != NULL &&
            humidity->published != NULL &&
            humidity->cell_size != 0 &&
            mesh.sheet_reach_m > 0.0f &&
            world->width % humidity->cell_size == 0 &&
            world->depth % humidity->cell_size == 0) {
            uint32_t humidity_columns = world->width / humidity->cell_size;
            uint32_t humidity_rows = world->depth / humidity->cell_size;
            float *cell_depth = calloc(cell_count, sizeof(*cell_depth));

            if (cell_depth == NULL) {
                free_mesh();
                return -1;
            }

            for (row = 0; row < mesh.cell_depth; row++) {
                for (column = 0; column < mesh.cell_width; column++) {
                    uint32_t east_mm =
                        column * heightmap->cell_size +
                        heightmap->cell_size / 2;
                    uint32_t north_mm =
                        row * heightmap->cell_size +
                        heightmap->cell_size / 2;
                    uint32_t humidity_column = east_mm / humidity->cell_size;
                    uint32_t humidity_row = north_mm / humidity->cell_size;
                    uint16_t value;

                    if (humidity_column >= humidity_columns) {
                        humidity_column = humidity_columns - 1;
                    }
                    if (humidity_row >= humidity_rows) {
                        humidity_row = humidity_rows - 1;
                    }
                    value = humidity->published[
                        humidity_row * humidity_columns + humidity_column];
                    cell_depth[row * mesh.cell_width + column] = (float)(
                        ((double)value / 65535.0) *
                        (double)mesh.sheet_reach_m);
                }
            }
            average_corners_from_cells(cell_depth, mesh.corner_humidity_m);
            free(cell_depth);
        }
    }

    return 0;
}

static const vec3 *corner_at(uint32_t column, uint32_t row)
{
    return &mesh.corners[row * mesh.corners_x + column];
}

static float face_shade(const vec3 *a, const vec3 *b, const vec3 *c)
{
    float ux = b->x - a->x;
    float uy = b->y - a->y;
    float uz = b->z - a->z;
    float vx = c->x - a->x;
    float vy = c->y - a->y;
    float vz = c->z - a->z;
    float nx = uy * vz - uz * vy;
    float ny = uz * vx - ux * vz;
    float nz = ux * vy - uy * vx;
    float length;
    float incidence;
    float shade;

    length = sqrtf(nx * nx + ny * ny + nz * nz);
    if (length == 0.0f) {
        incidence = 0.0f;
    } else {
        nx /= length;
        ny /= length;
        nz /= length;
        incidence = nx * light_x + ny * light_y + nz * light_z;
        if (incidence < 0.0f) {
            incidence = 0.0f;
        }
    }

    shade = view.diffuse + view.direct * incidence;
    shade += 0.2f * view.daylight;
    return clampf(shade, 0.0f, 1.0f);
}

static void emit_triangle(
    const vec3 *a,
    const vec3 *b,
    const vec3 *c,
    float shade,
    float red,
    float green,
    float blue,
    float alpha)
{
    glColor4f(shade * red, shade * green, shade * blue, alpha);
    glVertex3fv(&a->x);
    glVertex3fv(&b->x);
    glVertex3fv(&c->x);
}

static void draw_triangle(
    const vec3 *a,
    const vec3 *b,
    const vec3 *c,
    float red,
    float green,
    float blue)
{
    emit_triangle(a, b, c, face_shade(a, b, c), red, green, blue, 1.0f);
}

static vec3 water_corner(uint32_t column, uint32_t row)
{
    const vec3 *corner = corner_at(column, row);
    vec3 raised;

    raised = *corner;
    raised.z += mesh.corner_water_m[row * mesh.corners_x + column];
    return raised;
}

/* Same footprint as the terrain corner, hung from the heightmap zero. */
static vec3 undersheet_corner(
    uint32_t column,
    uint32_t row,
    const float *corner_depth_m)
{
    const vec3 *corner = corner_at(column, row);
    vec3 lowered;

    lowered.x = corner->x;
    lowered.y = corner->y;
    lowered.z = mesh.datum_m -
        corner_depth_m[row * mesh.corners_x + column];
    return lowered;
}

static void draw_water(void)
{
    uint32_t column;
    uint32_t row;

    glBegin(GL_TRIANGLES);
    for (row = 0; row < mesh.cell_depth; row++) {
        for (column = 0; column < mesh.cell_width; column++) {
            vec3 southwest;
            vec3 southeast;
            vec3 northeast;
            vec3 northwest;

            if (mesh.cell_water_m[row * mesh.cell_width + column] <= 0.0f) {
                continue;
            }

            southwest = water_corner(column, row);
            southeast = water_corner(column + 1, row);
            northeast = water_corner(column + 1, row + 1);
            northwest = water_corner(column, row + 1);
            emit_triangle(
                &southwest, &southeast, &northeast,
                face_shade(
                    corner_at(column, row),
                    corner_at(column + 1, row),
                    corner_at(column + 1, row + 1)),
                water_red, water_green, water_blue,
                1.0f);
            emit_triangle(
                &southwest, &northeast, &northwest,
                face_shade(
                    corner_at(column, row),
                    corner_at(column + 1, row + 1),
                    corner_at(column, row + 1)),
                water_red, water_green, water_blue,
                1.0f);
        }
    }
    glEnd();
}

static void draw_undersheet(
    const float *corner_depth_m,
    float red,
    float green,
    float blue)
{
    uint32_t column;
    uint32_t row;

    glBegin(GL_TRIANGLES);
    for (row = 0; row < mesh.cell_depth; row++) {
        for (column = 0; column < mesh.cell_width; column++) {
            float southwest_depth =
                corner_depth_m[row * mesh.corners_x + column];
            float southeast_depth =
                corner_depth_m[row * mesh.corners_x + column + 1];
            float northeast_depth =
                corner_depth_m[(row + 1) * mesh.corners_x + column + 1];
            float northwest_depth =
                corner_depth_m[(row + 1) * mesh.corners_x + column];
            vec3 southwest;
            vec3 southeast;
            vec3 northeast;
            vec3 northwest;

            if (southwest_depth <= 0.0f &&
                southeast_depth <= 0.0f &&
                northeast_depth <= 0.0f &&
                northwest_depth <= 0.0f) {
                continue;
            }

            southwest = undersheet_corner(column, row, corner_depth_m);
            southeast = undersheet_corner(column + 1, row, corner_depth_m);
            northeast = undersheet_corner(column + 1, row + 1, corner_depth_m);
            northwest = undersheet_corner(column, row + 1, corner_depth_m);
            draw_triangle(
                &southwest, &southeast, &northeast,
                red, green, blue);
            draw_triangle(
                &southwest, &northeast, &northwest,
                red, green, blue);
        }
    }
    glEnd();
}

static void draw_active_undersheet(void)
{
    if (undersheet_mode == UNDERSHEET_HUMIDITY) {
        draw_undersheet(
            mesh.corner_humidity_m,
            humidity_red,
            humidity_green,
            humidity_blue);
    } else if (undersheet_mode == UNDERSHEET_FERTILITY) {
        draw_undersheet(
            mesh.corner_fertility_m,
            fertility_red,
            fertility_green,
            fertility_blue);
    } else {
        draw_undersheet(
            mesh.corner_grassage_m,
            grassage_red,
            grassage_green,
            grassage_blue);
    }
}

static void draw_mesh(void)
{
    uint32_t column;
    uint32_t row;

    glBegin(GL_TRIANGLES);
    for (row = 0; row < mesh.cell_depth; row++) {
        for (column = 0; column < mesh.cell_width; column++) {
            const vec3 *southwest = corner_at(column, row);
            const vec3 *southeast = corner_at(column + 1, row);
            const vec3 *northeast = corner_at(column + 1, row + 1);
            const vec3 *northwest = corner_at(column, row + 1);

            draw_triangle(
                southwest, southeast, northeast,
                ground_red, ground_green, ground_blue);
            draw_triangle(
                southwest, northeast, northwest,
                ground_red, ground_green, ground_blue);
        }
    }
    glEnd();

    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glEnable(GL_POLYGON_OFFSET_FILL);
    glPolygonOffset(-1.0f, -1.0f);
    glDepthMask(GL_FALSE);
    glBegin(GL_TRIANGLES);
    for (row = 0; row < mesh.cell_depth; row++) {
        for (column = 0; column < mesh.cell_width; column++) {
            float alpha = mesh.cell_grass_alpha[row * mesh.cell_width + column];
            const vec3 *southwest;
            const vec3 *southeast;
            const vec3 *northeast;
            const vec3 *northwest;

            if (alpha <= 0.0f) {
                continue;
            }

            southwest = corner_at(column, row);
            southeast = corner_at(column + 1, row);
            northeast = corner_at(column + 1, row + 1);
            northwest = corner_at(column, row + 1);
            emit_triangle(
                southwest, southeast, northeast,
                face_shade(southwest, southeast, northeast),
                grass_red, grass_green, grass_blue,
                alpha);
            emit_triangle(
                southwest, northeast, northwest,
                face_shade(southwest, northeast, northwest),
                grass_red, grass_green, grass_blue,
                alpha);
        }
    }
    glEnd();
    glDepthMask(GL_TRUE);
    glDisable(GL_BLEND);

    glPolygonOffset(-2.0f, -2.0f);
    draw_water();
    glDisable(GL_POLYGON_OFFSET_FILL);

    draw_active_undersheet();
}

static void camera_eye(float *x, float *y, float *z)
{
    float horizontal = view.distance * cosf(view.elevation);

    *x = view.target_x + horizontal * sinf(view.azimuth);
    *y = view.target_y - horizontal * cosf(view.azimuth);
    *z = view.target_z + view.distance * sinf(view.elevation);
}

static void redraw(void)
{
    float eye_x;
    float eye_y;
    float eye_z;
    float aspect;

    if (window_width < 1) {
        window_width = 1;
    }
    if (window_height < 1) {
        window_height = 1;
    }

    aspect = (float)window_width / (float)window_height;

    glViewport(0, 0, window_width, window_height);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    gluPerspective(45.0, aspect, 0.05, view.distance + mesh.span_m * 20.0f);

    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
    camera_eye(&eye_x, &eye_y, &eye_z);
    gluLookAt(
        eye_x, eye_y, eye_z,
        view.target_x, view.target_y, view.target_z,
        0.0, 0.0, 1.0);

    draw_mesh();
    glXSwapBuffers(display, window);
}

static void clamp_view(void)
{
    float minimum = mesh.span_m * 0.05f;
    float maximum = mesh.span_m * 12.0f;

    if (minimum < 0.2f) {
        minimum = 0.2f;
    }
    view.distance = clampf(view.distance, minimum, maximum);
    view.elevation = clampf(view.elevation, -1.45f, 1.45f);
}

static void pan(float east, float north)
{
    view.target_x += east * view.pan_step;
    view.target_y += north * view.pan_step;
}

static void move_view(KeySym key)
{
    float forward_east = -sinf(view.azimuth);
    float forward_north = cosf(view.azimuth);
    float right_east = cosf(view.azimuth);
    float right_north = sinf(view.azimuth);

    if (key == XK_Up) {
        pan(forward_east, forward_north);
    } else if (key == XK_Down) {
        pan(-forward_east, -forward_north);
    } else if (key == XK_Right) {
        pan(right_east, right_north);
    } else if (key == XK_Left) {
        pan(-right_east, -right_north);
    }
}

static void adjust_light(KeySym key)
{
    if (key == XK_comma) {
        view.diffuse = clampf(view.diffuse - LIGHT_STEP, LIGHT_MIN, LIGHT_MAX);
        print_lights();
    } else if (key == XK_period) {
        view.diffuse = clampf(view.diffuse + LIGHT_STEP, LIGHT_MIN, LIGHT_MAX);
        print_lights();
    } else if (key == XK_bracketleft) {
        view.direct = clampf(view.direct - LIGHT_STEP, LIGHT_MIN, LIGHT_MAX);
        print_lights();
    } else if (key == XK_bracketright) {
        view.direct = clampf(view.direct + LIGHT_STEP, LIGHT_MIN, LIGHT_MAX);
        print_lights();
    }
}

static int open_window(void)
{
    int attributes[] = {
        GLX_RGBA,
        GLX_DOUBLEBUFFER,
        GLX_DEPTH_SIZE, 24,
        None
    };
    XVisualInfo *visual;
    XSetWindowAttributes window_attributes;
    Colormap colormap;
    GLXContext context;
    Atom delete_window;

    display = XOpenDisplay(NULL);
    if (display == NULL) {
        die("cannot open the X display");
    }

    visual = glXChooseVisual(display, DefaultScreen(display), attributes);
    if (visual == NULL) {
        die("no double-buffered OpenGL visual");
    }

    colormap = XCreateColormap(
        display,
        RootWindow(display, visual->screen),
        visual->visual,
        AllocNone);

    memset(&window_attributes, 0, sizeof(window_attributes));
    window_attributes.colormap = colormap;
    window_attributes.event_mask =
        ExposureMask |
        StructureNotifyMask |
        KeyPressMask |
        ButtonPressMask |
        ButtonReleaseMask |
        ButtonMotionMask;

    window = XCreateWindow(
        display,
        RootWindow(display, visual->screen),
        80, 80,
        (unsigned)window_width,
        (unsigned)window_height,
        0,
        visual->depth,
        InputOutput,
        visual->visual,
        CWColormap | CWEventMask,
        &window_attributes);

    XStoreName(display, window, "Conscious world-view");
    delete_window = XInternAtom(display, "WM_DELETE_WINDOW", False);
    XSetWMProtocols(display, window, &delete_window, 1);
    XMapWindow(display, window);

    context = glXCreateContext(display, visual, NULL, True);
    if (context == NULL) {
        die("cannot create an OpenGL context");
    }
    glXMakeCurrent(display, window, context);

    glEnable(GL_DEPTH_TEST);
    glClearColor(0.12f, 0.12f, 0.12f, 1.0f);
    return 0;
}

static void handle_event(XEvent *event, Atom delete_window, bool *running)
{
    if (event->type == Expose && event->xexpose.count == 0) {
        dirty = true;
        return;
    }

    if (event->type == ConfigureNotify) {
        window_width = event->xconfigure.width;
        window_height = event->xconfigure.height;
        dirty = true;
        return;
    }

    if (event->type == ButtonPress) {
        if (event->xbutton.button == Button1) {
            dragging = true;
            last_x = event->xbutton.x;
            last_y = event->xbutton.y;
        } else if (event->xbutton.button == Button4) {
            view.distance *= 0.9f;
            clamp_view();
            dirty = true;
        } else if (event->xbutton.button == Button5) {
            view.distance *= 1.1f;
            clamp_view();
            dirty = true;
        }
        return;
    }

    if (event->type == ButtonRelease && event->xbutton.button == Button1) {
        dragging = false;
        return;
    }

    if (event->type == MotionNotify && dragging) {
        int dx = event->xmotion.x - last_x;
        int dy = event->xmotion.y - last_y;

        last_x = event->xmotion.x;
        last_y = event->xmotion.y;
        view.azimuth += (float)dx * 0.007f;
        view.elevation -= (float)dy * 0.007f;
        clamp_view();
        dirty = true;
        return;
    }

    if (event->type == KeyPress) {
        KeySym key = XLookupKeysym(&event->xkey, 0);

        if (key == XK_Escape || key == XK_q) {
            *running = false;
            return;
        }
        if (key == XK_Tab) {
            undersheet_mode = (undersheet_mode + 1) % UNDERSHEET_COUNT;
            fprintf(
                stderr,
                "world-view: undersheet=%s\n",
                undersheet_name(undersheet_mode));
            dirty = true;
            return;
        }
        if (key == XK_comma || key == XK_period ||
            key == XK_bracketleft || key == XK_bracketright) {
            adjust_light(key);
            dirty = true;
            return;
        }
        move_view(key);
        dirty = true;
        return;
    }

    if (event->type == ClientMessage &&
        (Atom)event->xclient.data.l[0] == delete_window) {
        *running = false;
    }
}

/*
 * Drain the whole pending queue into view state, then draw at most once.
 * Events that arrive during redraw are drained afterward without stacking
 * one frame per mouse sample.
 */
static void event_loop(void)
{
    Atom delete_window = XInternAtom(display, "WM_DELETE_WINDOW", False);
    bool running = true;

    while (running) {
        XEvent event;

        if (XPending(display) == 0) {
            if (dirty) {
                dirty = false;
                redraw();
                continue;
            }
            XNextEvent(display, &event);
            handle_event(&event, delete_window, &running);
        }

        while (XPending(display) > 0) {
            XNextEvent(display, &event);
            handle_event(&event, delete_window, &running);
        }

        if (dirty) {
            dirty = false;
            redraw();
        }
    }
}

static void parse_args(int argc, char **argv, const char **world_path)
{
    int i;

    *world_path = NULL;
    view.diffuse = DEFAULT_DIFFUSE;
    view.direct = DEFAULT_DIRECT;

    for (i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            usage();
            exit(EXIT_SUCCESS);
        }
        if (strcmp(argv[i], "--diffuse") == 0) {
            if (i + 1 >= argc) {
                fprintf(stderr, "world-view: --diffuse needs a value\n");
                usage();
                exit(EXIT_FAILURE);
            }
            view.diffuse = parse_light(argv[++i], "--diffuse");
            continue;
        }
        if (strcmp(argv[i], "--direct") == 0) {
            if (i + 1 >= argc) {
                fprintf(stderr, "world-view: --direct needs a value\n");
                usage();
                exit(EXIT_FAILURE);
            }
            view.direct = parse_light(argv[++i], "--direct");
            continue;
        }
        if (argv[i][0] == '-') {
            fprintf(stderr, "world-view: unknown option %s\n", argv[i]);
            usage();
            exit(EXIT_FAILURE);
        }
        if (*world_path != NULL) {
            fprintf(stderr, "world-view: only one WORLD path is allowed\n");
            usage();
            exit(EXIT_FAILURE);
        }
        *world_path = argv[i];
    }

    if (*world_path == NULL) {
        usage();
        exit(EXIT_FAILURE);
    }

    view.diffuse = clampf(view.diffuse, LIGHT_MIN, LIGHT_MAX);
    view.direct = clampf(view.direct, LIGHT_MIN, LIGHT_MAX);
}

int main(int argc, char **argv)
{
    const char *world_path;
    world_state_t world;
    world_error_t error;
    uint32_t mid_column;
    uint32_t mid_row;
    const vec3 *middle;

    parse_args(argc, argv, &world_path);
    view.daylight = 0.0f;

    error = world_load(world_path, &world);
    if (error != WORLD_OK) {
        fprintf(
            stderr,
            "world-view: %s\n",
            world_error_string(error));
        world_destroy(&world);
        return EXIT_FAILURE;
    }
    view.daylight = clampf(file_daylight(&world), 0.0f, 1.0f);
    printf("Daylight: %.3f\n", view.daylight);
    print_lights();
    if (build_mesh(&world) != 0) {
        world_destroy(&world);
        die("world has no heightmap");
    }
    world_destroy(&world);

    mid_column = mesh.corners_x / 2;
    mid_row = mesh.corners_y / 2;
    middle = corner_at(mid_column, mid_row);
    view.target_x = middle->x;
    view.target_y = middle->y;
    view.target_z = middle->z;
    view.distance = mesh.span_m * 1.7f;
    view.azimuth = 0.6f;
    view.elevation = 0.55f;
    view.pan_step = mesh.span_m * 0.04f;
    if (view.pan_step < 0.05f) {
        view.pan_step = 0.05f;
    }
    clamp_view();

    open_window();
    event_loop();

    free_mesh();
    return EXIT_SUCCESS;
}
