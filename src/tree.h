#ifndef TREE_H
#define TREE_H

#include "raylib.h"
#include <stdbool.h>

typedef struct {
    Vector3 position;
    int parent;        // index into nodes array, -1 for root
    float thickness;   // computed via pipe model after growth
} TreeNode;

typedef struct {
    Vector3 position;
    bool alive;
} AttractionPoint;

// Spatial grid for acceleration
typedef struct {
    int *cells;        // flat array: each cell stores index into entries[]
    int *entries;      // linked list entries: entries[i] = next index, -1 = end
    int *item_cell;    // which cell each item maps to (for rebuilds)
    int grid_x, grid_y, grid_z;
    int cell_count;
    Vector3 origin;    // min corner of the grid
    float cell_size;
} SpatialGrid;

typedef struct {
    TreeNode *nodes;
    int node_count;
    int node_cap;

    AttractionPoint *points;
    int point_count;

    // Spatial acceleration
    SpatialGrid node_grid;

    // Algorithm parameters
    float influence_radius;  // di - max distance a point can influence a node
    float kill_distance;     // dk - remove point when node is within this distance
    float segment_length;    // D  - new branch segment length

    // Crown envelope (ellipsoid)
    Vector3 crown_center;
    Vector3 crown_radii;     // rx, ry, rz

    bool growth_complete;
    int iteration;
    int stagnant_iters;    // iterations since last point was killed
} Tree;

// Create a new tree with the given parameters
Tree tree_create(int num_attraction_points,
                 Vector3 crown_center, Vector3 crown_radii,
                 float trunk_height,
                 float influence_radius, float kill_distance,
                 float segment_length);

// Free all memory
void tree_destroy(Tree *tree);

// Perform one growth iteration. Returns number of new nodes added.
int tree_grow_step(Tree *tree);

// Recompute branch thickness using the pipe model
void tree_compute_thickness(Tree *tree);

// Reset the tree (regenerate attraction points and trunk)
void tree_reset(Tree *tree, int num_attraction_points);

#endif // TREE_H
