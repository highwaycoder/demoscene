# Trapped Knight

A psychedelic visualiser of chessboard-leaper phenomena on a spiral-numbered
infinite board — animated, glowing, and GPU-rendered in C.

## The two modes

Number the cells of an infinite chessboard in a square spiral: `1` at the
centre, then `2, 3, 4 …` spiralling outward.

**Trapped** — a single leaper starts on square `1` and each turn jumps to the
**lowest-numbered square it has not yet visited**, until every square it could
reach is already visited and it is *trapped*. The standard knight visits
**2016 squares**, ending on square **2084** ([OEIS A316667](https://oeis.org/A316667);
the famous "2016" is the term count — 2015 actual jumps). Drawn as a glowing
animated curve, with a comet endlessly retracing the route.

**Competitive** — several armies take turns placing a piece on the
lowest-numbered square that is empty and **not attacked by an opposing army**
(allies are friendly). Interlocking coloured territories emerge. In 2D the
territory grows outward as a disc; in **3D** (`dim = 3`) the same rule runs in
a lattice with 24-move 3D leapers, revealed shell by shell as a growing
multi-coloured sphere.

The pattern is the point, not the path — the reveal is fast by default; its
*growth rate* (shells per second) is the thing to tune.

Both work with any of nine leapers: Knight, Zebra, Antelope, Camel, Giraffe,
Ferz, Wazir, Alfil, Dabbaba.

The phenomenon was found independently by Neil Sloane and Jonas Karlsson, and
popularised by Numberphile — [The Trapped Knight](https://www.youtube.com/watch?v=RGQe8waGJ4w).

## How it works

The walk / placement is **sequential**, so it is computed once on the CPU
(microseconds — closed-form spiral arithmetic, no search). The result is
uploaded to the GPU and drawn as instanced glowing quads — one per path
segment or placed piece — with a capsule-SDF glow in `path.frag`. Additive
blending makes colours bloom where things overlap. Stack: GLFW + OpenGL 3.3
core + GLEW.

## Build & run (Debian/Ubuntu/WSL)

```sh
sudo apt-get update
sudo apt-get install -y build-essential libglfw3-dev libglew-dev
make
./trappedknight
```

Run it from this directory — it loads its shaders and `scenes.cfg` by relative
path. On startup it prints, e.g. `[walk] Knight : 2016 squares … square 2084`.

## Scenes

Everything on screen is driven by **`scenes.cfg`** — a plain, hand-editable
file of named presets. Press <kbd>Tab</kbd> to cycle them. Edit and save the
file while the program runs and it **hot-reloads**.

```
[Red vs Black]
mode    = competitive
dim     = 2
armies  = knight, knight
tint    = #ff2a3a, #2a6bff
pieces  = 5500

[3D Three Kingdoms]
mode    = competitive
dim     = 3
armies  = knight, zebra, camel
tint    = #ff3344, #33ff99, #ffcc33
pieces  = 120000
growth  = 75
```

As you find combinations that make cool patterns, just append them.

## Controls

| key            | action                                  |
|----------------|-----------------------------------------|
| `tab`          | next scene                              |
| `space` / `r`  | pause / restart the reveal              |
| `up` / `down`  | growth rate (shells revealed per second)|
| `,` / `.`      | step the reveal back / forward one shell|
| `-` / `=`      | fewer / more pieces (grow / shrink the sphere)|
| `c`            | cinematic camera (slow rotate + breathe)|
| `o`            | auto-cycle scenes on / off              |
| `t`            | comet trail on / off                    |
| left-drag      | rotate the view (orbit the sphere in 3D)|
| `w a s d`      | pan       `z` / `x` zoom      `b` fit   |
| `[` / `]`      | glow radius      `1`–`5` palette        |
| `v` / `f` / `g`| vsync / fullscreen / screenshot         |
| `h` / `esc`    | help / quit                             |

## GPU acceleration

The startup line `[gl] renderer: ...` shows what you got. On WSL the program
auto-selects the `d3d12` Mesa driver (it exports `GALLIUM_DRIVER=d3d12` when it
sees `/dev/dxg`), so OpenGL runs on the real GPU — expect `D3D12 (...)` rather
than `llvmpipe`. Set your own `GALLIUM_DRIVER` to override.

## Environment variables

| variable       | effect                                          |
|----------------|-------------------------------------------------|
| `TK_SCENE=N`   | start on scene N (0-based)                      |
| `TK_BENCH=1`   | disable vsync and log fps to stderr             |
| `TK_SHOT=1`    | render one frame, save a screenshot, exit       |
| `TK_WAYLAND=1` | use the Wayland backend instead of X11          |

## Notes

- `bg.frag`, `path.vert`, `path.frag` and `scenes.cfg` all hot-reload on save.
- Screenshots are binary PPM (`P6`); convert with `ffmpeg -i in.ppm out.png`.
- The trapped walk is capped at 120000 moves; competitive placement at 200000
  pieces. 3D scenes use `GL_MAX` blending so the sphere never blows out.
