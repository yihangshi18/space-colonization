# Space Colonization Tree Generator

Interactive 3D procedural tree generation using the **space colonization algorithm**, implemented in C with [raylib](https://www.raylib.com/).

Based on the paper:

> Adam Runions, Brendan Lane, and Przemyslaw Prusinkiewicz.
> **"Modeling Trees with a Space Colonization Algorithm"**
> Eurographics Workshop on Natural Phenomena, 2007.
> [PDF](https://www.algorithmicbotany.org/papers/colonization.egwnp2007.pdf)

<!-- Add a demo video or screenshots here -->
<!-- ![Demo](media/demo.gif) -->

## How It Works

The algorithm simulates how trees compete for space:

1. **Attraction points** are scattered inside an ellipsoid envelope representing the tree crown
2. Each point influences the **closest branch node** within a configurable radius
3. Branch nodes grow toward their associated points by extending a new segment in the averaged direction
4. Points are **removed** once a branch gets close enough (kill distance)
5. The process repeats until all points are consumed or growth stagnates
6. Branch **thickness** is computed using the pipe model (cross-sectional area proportional to downstream leaf count)

The result is a naturally branching tree structure that emerges from simple geometric rules.

## Building

### Prerequisites

- **GCC** (or any C11 compiler)
- **CMake** 3.14+
- **X11 development headers** (Linux):
  ```bash
  # Ubuntu/Debian
  sudo apt install libxrandr-dev libxinerama-dev libxcursor-dev libxi-dev libxext-dev libxrender-dev libxfixes-dev

  # Fedora
  sudo dnf install libXrandr-devel libXinerama-devel libXcursor-devel libXi-devel

  # Arch
  sudo pacman -S libxrandr libxinerama libxcursor libxi
  ```

Raylib is fetched and built automatically by CMake — no manual install needed.

### Build

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j$(nproc)
```

### Run

```bash
./build/space_colonization
```

## Controls

| Key | Action |
|-----|--------|
| `SPACE` | Advance one growth step |
| `A` | Toggle auto-growth |
| `P` | Toggle attraction point visibility |
| `R` | Reset & regenerate with current parameters |
| `TAB` | Toggle parameter panel |
| `UP/DOWN` | Select parameter |
| `LEFT/RIGHT` | Adjust selected parameter |
| `F11` | Toggle fullscreen |
| Left mouse drag | Orbit camera |
| Scroll wheel | Zoom in/out |

## Parameters

All parameters are adjustable in real-time via the in-app panel:

| Parameter | Default | Description |
|-----------|---------|-------------|
| Num Points | 2000 | Number of attraction points in the crown envelope |
| Influence Radius | 8.0 | Max distance a point can influence a branch node |
| Kill Distance | 1.5 | Points are removed when a node gets this close |
| Segment Length | 0.8 | Length of each new branch segment |
| Crown Radius X/Y/Z | 5/6/5 | Ellipsoid envelope dimensions |
| Crown Center Y | 12.0 | Height of the crown center |
| Trunk Height | 6.0 | Height of the initial trunk |
| Thickness Scale | 0.04 | Visual scaling of branch thickness |

Changes to influence radius, kill distance, and segment length take effect during growth.
Crown shape and point count take effect on reset (`R`).

## Project Structure

```
├── CMakeLists.txt        # Build system (fetches raylib via FetchContent)
├── src/
│   ├── main.c            # Rendering, camera, UI, controls
│   ├── tree.c            # Space colonization algorithm
│   └── tree.h            # Data structures and API
```

## Implementation Details

- **Spatial grid acceleration** for nearest-neighbor queries (avoids O(n*m) brute force)
- **Stagnation detection** — when remaining points can't be reached (opposing directions cancel out), the algorithm switches to per-point individual targeting before stopping gracefully
- **Pipe model** for branch thickness — cross-sectional area at any point equals the sum of all downstream leaf areas
- Points rendered as GL points (not spheres) for performance at high counts

## License

MIT

## References

- [Original paper (PDF)](https://www.algorithmicbotany.org/papers/colonization.egwnp2007.pdf)
- [Algorithmic Botany — University of Calgary](http://algorithmicbotany.org/)
- [raylib](https://www.raylib.com/)
