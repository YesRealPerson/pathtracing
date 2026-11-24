# Path tracer hotkeys

Letter keys are not case-sensitive, except `C` (cell render), which needs a
capital. The program has three modes. It starts in **Edit** mode. Press `R`
to render and `V` to view the BVH.

## Edit mode (mesh preview)

| Key / mouse | Action |
|---|---|
| `R` | Render (switch to Render mode) |
| `V` | BVH visualizer (switch to Visualize mode) |
| `Space` | Reset camera |
| `H` | Toggle HUD |
| Left drag | Rotate camera (or move the selected element) |
| Right drag | Pan camera |
| Scroll | Zoom |
| Left click | Select mesh element under cursor |
| `U` / `D` | Upsample / downsample selected mesh |
| `I` | Isotropic remesh of selected mesh |
| `F` / `S` / `C` | Flip / split / collapse selected edge |

## Render mode

Settings marked "re-render" stop the current render, apply the change and
start again. Display settings apply right away, without re-rendering.

| Key / mouse | Action | |
|---|---|---|
| `]` / `[` | Samples per pixel ×2 / ÷2 | re-render |
| `=` (`+`) / `-` | Area light samples ×2 / ÷2 | re-render |
| `.` (`>`) / `,` (`<`) | Max ray depth +1 / −1 | re-render |
| `H` | Toggle direct lighting: hemisphere vs. importance sampling (CPU path) | re-render |
| `K` / `L` | Lens radius −0.05 / +0.05 | re-render |
| `;` / `'` | Focal distance −0.1 / +0.1 | re-render |
| `X` / `Z` | Exposure +½ stop / −½ stop | display only |
| `T` | Toggle filmic (ACES) tone curve | display only |
| `N` / `M` | Highlights: compress more / less (white point ÷2 / ×2; first `N` turns it on at 16, `M` past 64 turns it off) | display only |
| `R` | Re-render | |
| `S` | Save screenshot | |
| `D` | Dump camera settings to `<scene>_cam_settings.txt` | |
| `E` | Back to Edit mode | |
| `V` | BVH visualizer | |
| `C` (capital) | Toggle cell render mode | |
| Left drag (cell mode) | Select a cell to render | re-render |
| Right click | Autofocus on the clicked point | re-render |

Exposure, filmic and highlights start from the `-x`, `-T` and `-W` command line options.
Highlight compression lowers only bright values: brightness `W` (after
exposure) maps to white, and brighter values roll off smoothly. It works on
brightness, not on each channel, so bright colored light keeps its hue. Lower
`W` means stronger compression.
Exposure prints its new value as stops in the console.

## Visualize mode (BVH viewer)

| Key / mouse | Action |
|---|---|
| `↑` | Go to parent node |
| `←` / `→` | Go to left / right child |
| `A` | Toggle ray display |
| `R` | Render (switch to Render mode) |
| `E` | Back to Edit mode |
| `Space` | Reset camera |
| Left drag / right drag / scroll | Rotate / pan / zoom |

## Command line equivalents

| Option | Setting |
|---|---|
| `-s <n>` | samples per pixel |
| `-l <n>` | area light samples |
| `-m <n>` | max ray depth |
| `-x <stops>` | exposure |
| `-T` | filmic tone curve |
| `-W <white>` | highlight compression white point (e.g. 4; 0 = off) |
| `-r <w> <h>` | resolution |
| `-f <file>` | render to file without a window |
