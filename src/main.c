#include "raylib.h"
#include "rlgl.h"
#include "raymath.h"
#include "tree.h"
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <math.h>

#define DEFAULT_WIDTH  1200
#define DEFAULT_HEIGHT 800

// Tunable parameter entry
typedef struct {
    const char *name;
    float value;
    float min_val;
    float max_val;
    float step;
} Param;

enum {
    PARAM_NUM_POINTS = 0,
    PARAM_INFLUENCE_RADIUS,
    PARAM_KILL_DISTANCE,
    PARAM_SEGMENT_LENGTH,
    PARAM_CROWN_RX,
    PARAM_CROWN_RY,
    PARAM_CROWN_RZ,
    PARAM_CROWN_HEIGHT,
    PARAM_TRUNK_HEIGHT,
    PARAM_THICKNESS_SCALE,
    PARAM_COUNT
};

// Custom orbit camera state
typedef struct {
    float yaw;       // horizontal angle (radians)
    float pitch;     // vertical angle (radians)
    float distance;  // distance from target
    Vector3 target;  // look-at point
} OrbitCam;

static void orbit_cam_update(OrbitCam *oc, Camera3D *cam) {
    // Mouse drag to rotate (left button)
    if (IsMouseButtonDown(MOUSE_BUTTON_LEFT)) {
        Vector2 delta = GetMouseDelta();
        oc->yaw   -= delta.x * 0.005f;
        oc->pitch -= delta.y * 0.005f;
    }

    // Clamp pitch so we don't flip
    if (oc->pitch >  1.4f) oc->pitch =  1.4f;
    if (oc->pitch < -0.2f) oc->pitch = -0.2f;

    // Scroll to zoom
    float wheel = GetMouseWheelMove();
    oc->distance -= wheel * oc->distance * 0.1f;
    if (oc->distance < 3.0f) oc->distance = 3.0f;
    if (oc->distance > 200.0f) oc->distance = 200.0f;

    // Compute camera position from spherical coordinates
    cam->target = oc->target;
    cam->position = (Vector3){
        oc->target.x + oc->distance * cosf(oc->pitch) * sinf(oc->yaw),
        oc->target.y + oc->distance * sinf(oc->pitch),
        oc->target.z + oc->distance * cosf(oc->pitch) * cosf(oc->yaw),
    };
    cam->up = (Vector3){ 0, 1, 0 };
}

// Compute ideal camera distance and target from tree parameters
static void orbit_cam_fit(OrbitCam *oc, Param *params) {
    float crown_y = params[PARAM_CROWN_HEIGHT].value;
    float crown_ry = params[PARAM_CROWN_RY].value;
    float trunk_h = params[PARAM_TRUNK_HEIGHT].value;

    // Target: vertical center of the whole tree (ground to crown top)
    float tree_top = crown_y + crown_ry;
    oc->target = (Vector3){ 0, tree_top * 0.45f, 0 };

    // Distance: enough to see the full height + width with some margin
    float max_radius = params[PARAM_CROWN_RX].value;
    if (params[PARAM_CROWN_RZ].value > max_radius) max_radius = params[PARAM_CROWN_RZ].value;
    float extent = tree_top > max_radius * 2 ? tree_top : max_radius * 2;
    oc->distance = extent * 1.8f;
    if (oc->distance < 10.0f) oc->distance = 10.0f;
}

static void draw_tree(const Tree *tree, float thickness_scale) {
    for (int i = 0; i < tree->node_count; i++) {
        int pi = tree->nodes[i].parent;
        if (pi < 0) continue;

        Vector3 start = tree->nodes[pi].position;
        Vector3 end   = tree->nodes[i].position;

        float r1 = tree->nodes[pi].thickness * thickness_scale;
        float r2 = tree->nodes[i].thickness  * thickness_scale;

        DrawCylinderEx(start, end, r1, r2, 6, BROWN);
    }
}

// Fast point rendering using GL points instead of spheres
static void draw_attraction_points(const Tree *tree) {
    rlEnablePointMode();
    rlBegin(RL_LINES);
    Color c = { 50, 220, 50, 180 };
    for (int i = 0; i < tree->point_count; i++) {
        if (!tree->points[i].alive) continue;
        rlColor4ub(c.r, c.g, c.b, c.a);
        rlVertex3f(tree->points[i].position.x,
                    tree->points[i].position.y,
                    tree->points[i].position.z);
    }
    rlEnd();
    rlDisableWireMode();
}

static void draw_crown_wireframe(Vector3 center, Vector3 radii) {
    int segments = 48;
    for (int i = 0; i < segments; i++) {
        float a0 = (float)i / segments * 2.0f * PI;
        float a1 = (float)(i + 1) / segments * 2.0f * PI;

        Vector3 p0, p1;

        p0 = (Vector3){ center.x + cosf(a0)*radii.x, center.y + sinf(a0)*radii.y, center.z };
        p1 = (Vector3){ center.x + cosf(a1)*radii.x, center.y + sinf(a1)*radii.y, center.z };
        DrawLine3D(p0, p1, (Color){ 255, 255, 255, 40 });

        p0 = (Vector3){ center.x + cosf(a0)*radii.x, center.y, center.z + sinf(a0)*radii.z };
        p1 = (Vector3){ center.x + cosf(a1)*radii.x, center.y, center.z + sinf(a1)*radii.z };
        DrawLine3D(p0, p1, (Color){ 255, 255, 255, 40 });

        p0 = (Vector3){ center.x, center.y + cosf(a0)*radii.y, center.z + sinf(a0)*radii.z };
        p1 = (Vector3){ center.x, center.y + cosf(a1)*radii.y, center.z + sinf(a1)*radii.z };
        DrawLine3D(p0, p1, (Color){ 255, 255, 255, 40 });
    }
}

static void draw_ground(void) {
    DrawPlane((Vector3){ 0, 0, 0 }, (Vector2){ 40, 40 }, (Color){ 50, 120, 50, 255 });
    DrawGrid(40, 1.0f);
}

static void draw_hud(const Tree *tree, bool auto_grow, bool show_points,
                     Param *params, int selected_param, bool panel_open, int fps) {
    int sw = GetScreenWidth();
    int y = 10;
    DrawText(TextFormat("FPS: %d", fps), 10, y, 20, LIME); y += 25;
    DrawText(TextFormat("Nodes: %d", tree->node_count), 10, y, 20, WHITE); y += 25;
    DrawText(TextFormat("Iteration: %d", tree->iteration), 10, y, 20, WHITE); y += 25;

    int alive = 0;
    for (int i = 0; i < tree->point_count; i++)
        if (tree->points[i].alive) alive++;
    DrawText(TextFormat("Attraction pts: %d / %d", alive, tree->point_count), 10, y, 20, WHITE); y += 25;

    if (tree->growth_complete)
        DrawText("GROWTH COMPLETE", 10, y, 20, GREEN);
    else
        DrawText(auto_grow ? "Auto-growing..." : "Paused", 10, y, 20, YELLOW);
    y += 35;

    DrawText("[SPACE] Step  [A] Auto  [P] Points  [R] Reset", 10, y, 16, LIGHTGRAY); y += 20;
    DrawText("[TAB] Params  [F11] Fullscreen  [LMB] Orbit  [Scroll] Zoom", 10, y, 16, LIGHTGRAY);
    y += 5;

    // Parameter panel (right side)
    if (panel_open) {
        int pw = 380;
        int px = sw - pw - 10;
        int py = 10;
        int ph = PARAM_COUNT * 28 + 50;

        DrawRectangle(px, py, pw, ph, (Color){ 0, 0, 0, 180 });
        DrawRectangleLines(px, py, pw, ph, LIGHTGRAY);

        DrawText("PARAMETERS  [UP/DOWN] select  [LEFT/RIGHT] adjust", px + 10, py + 8, 14, WHITE);
        py += 32;

        for (int i = 0; i < PARAM_COUNT; i++) {
            Color col = (i == selected_param) ? YELLOW : LIGHTGRAY;
            const char *arrow = (i == selected_param) ? "> " : "  ";

            if (i == PARAM_NUM_POINTS) {
                DrawText(TextFormat("%s%-20s %6d", arrow, params[i].name, (int)params[i].value),
                         px + 10, py, 16, col);
            } else {
                DrawText(TextFormat("%s%-20s %6.2f", arrow, params[i].name, params[i].value),
                         px + 10, py, 16, col);
            }
            py += 28;
        }
    }
}

static Tree rebuild_tree(Param *params) {
    return tree_create(
        (int)params[PARAM_NUM_POINTS].value,
        (Vector3){ 0, params[PARAM_CROWN_HEIGHT].value, 0 },
        (Vector3){ params[PARAM_CROWN_RX].value, params[PARAM_CROWN_RY].value, params[PARAM_CROWN_RZ].value },
        params[PARAM_TRUNK_HEIGHT].value,
        params[PARAM_INFLUENCE_RADIUS].value,
        params[PARAM_KILL_DISTANCE].value,
        params[PARAM_SEGMENT_LENGTH].value
    );
}

int main(void) {
    srand((unsigned)time(NULL));

    SetConfigFlags(FLAG_WINDOW_RESIZABLE | FLAG_MSAA_4X_HINT);
    InitWindow(DEFAULT_WIDTH, DEFAULT_HEIGHT, "Space Colonization - Tree Generator");
    SetTargetFPS(60);

    Camera3D camera = {0};
    camera.fovy = 45.0f;
    camera.projection = CAMERA_PERSPECTIVE;

    // Tunable parameters
    Param params[PARAM_COUNT] = {
        [PARAM_NUM_POINTS]       = { "Num Points",        2000, 100, 10000, 100 },
        [PARAM_INFLUENCE_RADIUS] = { "Influence Radius",  8.0f, 1.0f, 30.0f, 0.5f },
        [PARAM_KILL_DISTANCE]    = { "Kill Distance",     1.5f, 0.2f, 10.0f, 0.1f },
        [PARAM_SEGMENT_LENGTH]   = { "Segment Length",    0.8f, 0.1f,  5.0f, 0.1f },
        [PARAM_CROWN_RX]         = { "Crown Radius X",   5.0f, 1.0f, 20.0f, 0.5f },
        [PARAM_CROWN_RY]         = { "Crown Radius Y",   6.0f, 1.0f, 20.0f, 0.5f },
        [PARAM_CROWN_RZ]         = { "Crown Radius Z",   5.0f, 1.0f, 20.0f, 0.5f },
        [PARAM_CROWN_HEIGHT]     = { "Crown Center Y",   12.0f, 3.0f, 40.0f, 0.5f },
        [PARAM_TRUNK_HEIGHT]     = { "Trunk Height",     6.0f, 1.0f, 30.0f, 0.5f },
        [PARAM_THICKNESS_SCALE]  = { "Thickness Scale",  0.04f, 0.005f, 0.2f, 0.005f },
    };

    // Orbit camera - auto-fit to tree on start
    OrbitCam orbit = { .yaw = 0.8f, .pitch = 0.4f };
    orbit_cam_fit(&orbit, params);

    Tree tree = rebuild_tree(params);

    bool auto_grow = false;
    bool show_points = true;
    bool panel_open = true;
    int selected_param = 0;
    int grow_every_n_frames = 3;
    int frame_counter = 0;
    bool is_fullscreen = false;
    int windowed_width = DEFAULT_WIDTH;
    int windowed_height = DEFAULT_HEIGHT;

    // For held-key repeat
    float key_repeat_timer = 0.0f;
    float key_repeat_delay = 0.35f;
    float key_repeat_rate  = 0.07f;

    while (!WindowShouldClose()) {
        float dt = GetFrameTime();

        // --- Fullscreen toggle ---
        if (IsKeyPressed(KEY_F11)) {
            if (!is_fullscreen) {
                windowed_width = GetScreenWidth();
                windowed_height = GetScreenHeight();
                int monitor = GetCurrentMonitor();
                SetWindowSize(GetMonitorWidth(monitor), GetMonitorHeight(monitor));
                ToggleFullscreen();
                is_fullscreen = true;
            } else {
                ToggleFullscreen();
                SetWindowSize(windowed_width, windowed_height);
                is_fullscreen = false;
            }
        }

        // --- Input ---
        if (IsKeyPressed(KEY_SPACE) && !tree.growth_complete) {
            tree_grow_step(&tree);
        }
        if (IsKeyPressed(KEY_A)) {
            auto_grow = !auto_grow;
        }
        if (IsKeyPressed(KEY_P)) {
            show_points = !show_points;
        }
        if (IsKeyPressed(KEY_TAB)) {
            panel_open = !panel_open;
        }
        if (IsKeyPressed(KEY_R)) {
            tree_destroy(&tree);
            tree = rebuild_tree(params);
            orbit_cam_fit(&orbit, params);
            auto_grow = false;
        }

        // Parameter selection
        if (panel_open) {
            if (IsKeyPressed(KEY_UP))   { selected_param--; if (selected_param < 0) selected_param = PARAM_COUNT - 1; }
            if (IsKeyPressed(KEY_DOWN)) { selected_param++; if (selected_param >= PARAM_COUNT) selected_param = 0; }

            int dir = 0;
            if (IsKeyPressed(KEY_LEFT) || IsKeyPressed(KEY_RIGHT)) {
                dir = IsKeyPressed(KEY_RIGHT) ? 1 : -1;
                key_repeat_timer = -key_repeat_delay;
            } else if (IsKeyDown(KEY_LEFT) || IsKeyDown(KEY_RIGHT)) {
                key_repeat_timer += dt;
                if (key_repeat_timer >= key_repeat_rate) {
                    dir = IsKeyDown(KEY_RIGHT) ? 1 : -1;
                    key_repeat_timer = 0.0f;
                }
            } else {
                key_repeat_timer = 0.0f;
            }

            if (dir != 0) {
                Param *p = &params[selected_param];
                p->value += dir * p->step;
                if (p->value < p->min_val) p->value = p->min_val;
                if (p->value > p->max_val) p->value = p->max_val;

                tree.influence_radius = params[PARAM_INFLUENCE_RADIUS].value;
                tree.kill_distance    = params[PARAM_KILL_DISTANCE].value;
                tree.segment_length   = params[PARAM_SEGMENT_LENGTH].value;

                // Re-fit camera target when crown/trunk params change
                int sp = selected_param;
                if (sp == PARAM_CROWN_RX || sp == PARAM_CROWN_RY || sp == PARAM_CROWN_RZ ||
                    sp == PARAM_CROWN_HEIGHT || sp == PARAM_TRUNK_HEIGHT) {
                    orbit_cam_fit(&orbit, params);
                }
            }
        }

        // Auto-grow
        if (auto_grow && !tree.growth_complete) {
            frame_counter++;
            if (frame_counter >= grow_every_n_frames) {
                tree_grow_step(&tree);
                frame_counter = 0;
            }
        }

        // Camera update (custom orbit, no keyboard conflicts)
        orbit_cam_update(&orbit, &camera);

        float thickness_scale = params[PARAM_THICKNESS_SCALE].value;

        // --- Draw ---
        BeginDrawing();
            ClearBackground((Color){ 30, 30, 40, 255 });

            BeginMode3D(camera);
                draw_ground();
                draw_tree(&tree, thickness_scale);
                if (show_points) draw_attraction_points(&tree);
                draw_crown_wireframe(
                    (Vector3){ 0, params[PARAM_CROWN_HEIGHT].value, 0 },
                    (Vector3){ params[PARAM_CROWN_RX].value, params[PARAM_CROWN_RY].value, params[PARAM_CROWN_RZ].value }
                );
            EndMode3D();

            draw_hud(&tree, auto_grow, show_points, params, selected_param, panel_open, GetFPS());
        EndDrawing();
    }

    tree_destroy(&tree);
    CloseWindow();

    return 0;
}
