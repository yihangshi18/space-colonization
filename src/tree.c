#include "tree.h"
#include "raymath.h"
#include <stdlib.h>
#include <math.h>
#include <string.h>

// ---- Helpers ----

static float randf(float lo, float hi) {
    return lo + (float)rand() / (float)RAND_MAX * (hi - lo);
}

static Vector3 random_in_unit_sphere(void) {
    Vector3 v;
    do {
        v = (Vector3){ randf(-1, 1), randf(-1, 1), randf(-1, 1) };
    } while (v.x*v.x + v.y*v.y + v.z*v.z > 1.0f);
    return v;
}

static inline int mini(int a, int b) { return a < b ? a : b; }
static inline int maxi(int a, int b) { return a > b ? a : b; }
static inline float minf(float a, float b) { return a < b ? a : b; }
static inline float maxf(float a, float b) { return a > b ? a : b; }

// ---- Spatial Grid ----

static void grid_init(SpatialGrid *g, Vector3 world_min, Vector3 world_max, float cell_size) {
    g->cell_size = cell_size;
    g->origin = world_min;

    g->grid_x = (int)ceilf((world_max.x - world_min.x) / cell_size) + 1;
    g->grid_y = (int)ceilf((world_max.y - world_min.y) / cell_size) + 1;
    g->grid_z = (int)ceilf((world_max.z - world_min.z) / cell_size) + 1;

    // Clamp to reasonable size
    if (g->grid_x > 128) g->grid_x = 128;
    if (g->grid_y > 128) g->grid_y = 128;
    if (g->grid_z > 128) g->grid_z = 128;
    if (g->grid_x < 1) g->grid_x = 1;
    if (g->grid_y < 1) g->grid_y = 1;
    if (g->grid_z < 1) g->grid_z = 1;

    g->cell_count = g->grid_x * g->grid_y * g->grid_z;
    g->cells = malloc(sizeof(int) * g->cell_count);
    g->entries = NULL;
    g->item_cell = NULL;
}

static void grid_free(SpatialGrid *g) {
    free(g->cells);
    free(g->entries);
    free(g->item_cell);
    g->cells = NULL;
    g->entries = NULL;
    g->item_cell = NULL;
}

static inline int grid_cell_index(const SpatialGrid *g, Vector3 pos) {
    int cx = (int)((pos.x - g->origin.x) / g->cell_size);
    int cy = (int)((pos.y - g->origin.y) / g->cell_size);
    int cz = (int)((pos.z - g->origin.z) / g->cell_size);
    cx = maxi(0, mini(cx, g->grid_x - 1));
    cy = maxi(0, mini(cy, g->grid_y - 1));
    cz = maxi(0, mini(cz, g->grid_z - 1));
    return cx + cy * g->grid_x + cz * g->grid_x * g->grid_y;
}

// Build grid from an array of positions
static void grid_build(SpatialGrid *g, Vector3 *positions, int count) {
    for (int i = 0; i < g->cell_count; i++) g->cells[i] = -1;

    free(g->entries);
    free(g->item_cell);
    g->entries = malloc(sizeof(int) * count);
    g->item_cell = malloc(sizeof(int) * count);

    for (int i = 0; i < count; i++) {
        int ci = grid_cell_index(g, positions[i]);
        g->item_cell[i] = ci;
        g->entries[i] = g->cells[ci];
        g->cells[ci] = i;
    }
}

// ---- Internal ----

static void ensure_node_capacity(Tree *tree, int needed) {
    if (tree->node_count + needed <= tree->node_cap) return;
    int new_cap = tree->node_cap * 2;
    if (new_cap < tree->node_count + needed) new_cap = tree->node_count + needed;
    tree->nodes = realloc(tree->nodes, sizeof(TreeNode) * new_cap);
    tree->node_cap = new_cap;
}

static int add_node(Tree *tree, Vector3 pos, int parent) {
    ensure_node_capacity(tree, 1);
    int idx = tree->node_count++;
    tree->nodes[idx].position = pos;
    tree->nodes[idx].parent = parent;
    tree->nodes[idx].thickness = 1.0f;
    return idx;
}

static void generate_attraction_points(Tree *tree, int count) {
    free(tree->points);
    tree->points = malloc(sizeof(AttractionPoint) * count);
    tree->point_count = count;

    for (int i = 0; i < count; i++) {
        Vector3 s = random_in_unit_sphere();
        tree->points[i].position = (Vector3){
            tree->crown_center.x + s.x * tree->crown_radii.x,
            tree->crown_center.y + s.y * tree->crown_radii.y,
            tree->crown_center.z + s.z * tree->crown_radii.z,
        };
        tree->points[i].alive = true;
    }
}

static void generate_trunk(Tree *tree, float trunk_height) {
    int num_trunk_nodes = (int)(trunk_height / tree->segment_length) + 1;
    if (num_trunk_nodes < 2) num_trunk_nodes = 2;

    float step = trunk_height / (num_trunk_nodes - 1);

    for (int i = 0; i < num_trunk_nodes; i++) {
        float y = i * step;
        int parent = (i == 0) ? -1 : i - 1;
        add_node(tree, (Vector3){ 0, y, 0 }, parent);
    }
}

// Compute bounding box of all relevant positions (nodes + alive points)
static void compute_world_bounds(Tree *tree, Vector3 *out_min, Vector3 *out_max) {
    float big = 1e9f;
    Vector3 lo = { big, big, big };
    Vector3 hi = { -big, -big, -big };

    for (int i = 0; i < tree->node_count; i++) {
        Vector3 p = tree->nodes[i].position;
        lo.x = minf(lo.x, p.x); lo.y = minf(lo.y, p.y); lo.z = minf(lo.z, p.z);
        hi.x = maxf(hi.x, p.x); hi.y = maxf(hi.y, p.y); hi.z = maxf(hi.z, p.z);
    }
    for (int i = 0; i < tree->point_count; i++) {
        if (!tree->points[i].alive) continue;
        Vector3 p = tree->points[i].position;
        lo.x = minf(lo.x, p.x); lo.y = minf(lo.y, p.y); lo.z = minf(lo.z, p.z);
        hi.x = maxf(hi.x, p.x); hi.y = maxf(hi.y, p.y); hi.z = maxf(hi.z, p.z);
    }

    // Pad a little
    float pad = 1.0f;
    lo.x -= pad; lo.y -= pad; lo.z -= pad;
    hi.x += pad; hi.y += pad; hi.z += pad;

    *out_min = lo;
    *out_max = hi;
}

static void rebuild_node_grid(Tree *tree) {
    Vector3 lo, hi;
    compute_world_bounds(tree, &lo, &hi);

    grid_free(&tree->node_grid);
    // Cell size ~ kill_distance or segment_length, whichever is larger
    float cs = maxf(tree->kill_distance, tree->segment_length);
    if (cs < 0.5f) cs = 0.5f;
    grid_init(&tree->node_grid, lo, hi, cs);

    // Build from node positions
    Vector3 *positions = malloc(sizeof(Vector3) * tree->node_count);
    for (int i = 0; i < tree->node_count; i++)
        positions[i] = tree->nodes[i].position;
    grid_build(&tree->node_grid, positions, tree->node_count);
    free(positions);
}

// ---- Public API ----

Tree tree_create(int num_attraction_points,
                 Vector3 crown_center, Vector3 crown_radii,
                 float trunk_height,
                 float influence_radius, float kill_distance,
                 float segment_length)
{
    Tree tree = {0};
    tree.node_cap = 4096;
    tree.nodes = malloc(sizeof(TreeNode) * tree.node_cap);
    tree.node_count = 0;
    tree.points = NULL;
    tree.point_count = 0;

    memset(&tree.node_grid, 0, sizeof(SpatialGrid));

    tree.influence_radius = influence_radius;
    tree.kill_distance = kill_distance;
    tree.segment_length = segment_length;

    tree.crown_center = crown_center;
    tree.crown_radii = crown_radii;

    tree.growth_complete = false;
    tree.iteration = 0;
    tree.stagnant_iters = 0;

    generate_attraction_points(&tree, num_attraction_points);
    generate_trunk(&tree, trunk_height);

    return tree;
}

void tree_destroy(Tree *tree) {
    free(tree->nodes);
    free(tree->points);
    grid_free(&tree->node_grid);
    tree->nodes = NULL;
    tree->points = NULL;
    tree->node_count = 0;
    tree->point_count = 0;
}

// Find closest node to position within max_dist2, using spatial grid.
// Returns node index or -1.
static int find_closest_node(Tree *tree, SpatialGrid *g, Vector3 pos, float max_dist2, int cell_range) {
    int cx = (int)((pos.x - g->origin.x) / g->cell_size);
    int cy = (int)((pos.y - g->origin.y) / g->cell_size);
    int cz = (int)((pos.z - g->origin.z) / g->cell_size);

    int closest = -1;
    float closest_d2 = max_dist2;

    int x0 = maxi(0, cx - cell_range), x1 = mini(g->grid_x - 1, cx + cell_range);
    int y0 = maxi(0, cy - cell_range), y1 = mini(g->grid_y - 1, cy + cell_range);
    int z0 = maxi(0, cz - cell_range), z1 = mini(g->grid_z - 1, cz + cell_range);

    for (int iz = z0; iz <= z1; iz++)
    for (int iy = y0; iy <= y1; iy++)
    for (int ix = x0; ix <= x1; ix++) {
        int ci = ix + iy * g->grid_x + iz * g->grid_x * g->grid_y;
        for (int ni = g->cells[ci]; ni != -1; ni = g->entries[ni]) {
            float d2 = Vector3LengthSqr(Vector3Subtract(pos, tree->nodes[ni].position));
            if (d2 < closest_d2) {
                closest_d2 = d2;
                closest = ni;
            }
        }
    }
    return closest;
}

// Check if any node is within dist2 of position, using spatial grid.
static bool any_node_within(Tree *tree, SpatialGrid *g, Vector3 pos, float dist2, int cell_range) {
    int cx = (int)((pos.x - g->origin.x) / g->cell_size);
    int cy = (int)((pos.y - g->origin.y) / g->cell_size);
    int cz = (int)((pos.z - g->origin.z) / g->cell_size);

    int x0 = maxi(0, cx - cell_range), x1 = mini(g->grid_x - 1, cx + cell_range);
    int y0 = maxi(0, cy - cell_range), y1 = mini(g->grid_y - 1, cy + cell_range);
    int z0 = maxi(0, cz - cell_range), z1 = mini(g->grid_z - 1, cz + cell_range);

    for (int iz = z0; iz <= z1; iz++)
    for (int iy = y0; iy <= y1; iy++)
    for (int ix = x0; ix <= x1; ix++) {
        int ci = ix + iy * g->grid_x + iz * g->grid_x * g->grid_y;
        for (int ni = g->cells[ci]; ni != -1; ni = g->entries[ni]) {
            if (Vector3LengthSqr(Vector3Subtract(pos, tree->nodes[ni].position)) < dist2)
                return true;
        }
    }
    return false;
}

#define STAGNATION_LIMIT 15

int tree_grow_step(Tree *tree) {
    if (tree->growth_complete) return 0;

    int n_nodes = tree->node_count;
    int n_points = tree->point_count;

    // Rebuild spatial grid with current nodes
    rebuild_node_grid(tree);
    SpatialGrid *g = &tree->node_grid;

    Vector3 *direction = calloc(n_nodes, sizeof(Vector3));
    int *assoc_count = calloc(n_nodes, sizeof(int));

    float di = tree->influence_radius;
    float di2 = di * di;
    int cell_range = (int)ceilf(di / g->cell_size);

    // Step 1: Associate each alive attraction point with its closest node
    for (int p = 0; p < n_points; p++) {
        if (!tree->points[p].alive) continue;

        int closest = find_closest_node(tree, g, tree->points[p].position, di2, cell_range);

        if (closest >= 0) {
            Vector3 dir = Vector3Normalize(
                Vector3Subtract(tree->points[p].position, tree->nodes[closest].position)
            );
            direction[closest] = Vector3Add(direction[closest], dir);
            assoc_count[closest]++;
        }
    }

    // Step 2: Grow new nodes
    // When stagnating, grow toward each point individually instead of averaging.
    // This prevents the case where opposing points cancel out and the tree never
    // reaches any of them.
    int new_nodes = 0;
    bool use_individual = (tree->stagnant_iters >= STAGNATION_LIMIT / 2);

    if (use_individual) {
        // Grow a dedicated branch toward each remaining point's closest node
        // Build a temporary set: for each point, record (closest_node, direction_to_point)
        for (int p = 0; p < n_points; p++) {
            if (!tree->points[p].alive) continue;

            int closest = find_closest_node(tree, g, tree->points[p].position, di2, cell_range);
            if (closest < 0) continue;

            Vector3 dir = Vector3Normalize(
                Vector3Subtract(tree->points[p].position, tree->nodes[closest].position)
            );
            Vector3 new_pos = Vector3Add(
                tree->nodes[closest].position,
                Vector3Scale(dir, tree->segment_length)
            );

            // Avoid duplicate nodes at nearly the same position
            float min_sep2 = (tree->segment_length * 0.3f) * (tree->segment_length * 0.3f);
            int kill_r = (int)ceilf(tree->segment_length * 0.3f / g->cell_size);
            if (!any_node_within(tree, g, new_pos, min_sep2, kill_r)) {
                add_node(tree, new_pos, closest);
                new_nodes++;
            }
        }
    } else {
        // Normal averaged growth
        for (int n = 0; n < n_nodes; n++) {
            if (assoc_count[n] == 0) continue;

            Vector3 avg_dir = Vector3Normalize(direction[n]);
            Vector3 new_pos = Vector3Add(
                tree->nodes[n].position,
                Vector3Scale(avg_dir, tree->segment_length)
            );

            add_node(tree, new_pos, n);
            new_nodes++;
        }
    }

    free(direction);
    free(assoc_count);

    // Step 3: Kill attraction points near any tree node
    rebuild_node_grid(tree);
    g = &tree->node_grid;

    float dk = tree->kill_distance;
    float dk2 = dk * dk;
    int kill_range = (int)ceilf(dk / g->cell_size);

    int killed = 0;
    for (int p = 0; p < n_points; p++) {
        if (!tree->points[p].alive) continue;

        if (any_node_within(tree, g, tree->points[p].position, dk2, kill_range)) {
            tree->points[p].alive = false;
            killed++;
        }
    }

    tree->iteration++;

    // Track stagnation
    if (killed > 0) {
        tree->stagnant_iters = 0;
    } else {
        tree->stagnant_iters++;
    }

    // Stop if: no new nodes, OR stagnating too long
    if (new_nodes == 0 || tree->stagnant_iters >= STAGNATION_LIMIT) {
        tree->growth_complete = true;
    }

    tree_compute_thickness(tree);

    return new_nodes;
}

void tree_compute_thickness(Tree *tree) {
    int n = tree->node_count;
    if (n == 0) return;

    int *child_count = calloc(n, sizeof(int));
    float *subtree_leaves = calloc(n, sizeof(float));

    for (int i = 0; i < n; i++) {
        if (tree->nodes[i].parent >= 0) {
            child_count[tree->nodes[i].parent]++;
        }
    }

    for (int i = 0; i < n; i++) {
        subtree_leaves[i] = (child_count[i] == 0) ? 1.0f : 0.0f;
    }

    for (int i = n - 1; i >= 0; i--) {
        if (tree->nodes[i].parent >= 0) {
            subtree_leaves[tree->nodes[i].parent] += subtree_leaves[i];
        }
    }

    for (int i = 0; i < n; i++) {
        tree->nodes[i].thickness = sqrtf(subtree_leaves[i]);
    }

    free(child_count);
    free(subtree_leaves);
}

void tree_reset(Tree *tree, int num_attraction_points) {
    tree->node_count = 0;
    tree->growth_complete = false;
    tree->iteration = 0;
    tree->stagnant_iters = 0;

    grid_free(&tree->node_grid);

    generate_attraction_points(tree, num_attraction_points);

    float trunk_height = tree->crown_center.y - tree->crown_radii.y;
    if (trunk_height < tree->segment_length * 2) trunk_height = tree->segment_length * 2;
    generate_trunk(tree, trunk_height);
}
