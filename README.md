# crosshair

Hyprland plugin that replaces the normal cursor with a CAD-style crosshair.

The plugin draws directly inside Hyprland's software cursor render path. There
is no GTK window, no layer-shell overlay, and no separate process to keep
running.

## Features

- Hides the normal cursor by skipping Hyprland's software cursor draw
- Forces software cursor mode while loaded, then restores it on unload
- Draws a full-screen dashed crosshair on the monitor containing the pointer
- Keeps the rotating HUD visible all the time
- Changes only the center marker for cursor intent:
  - bigger dot for links/pointers
  - I-beam for text
  - horizontal I-beam for vertical text
  - marks for move/grab/resize/not-allowed/wait
- Reads cursor position and cursor shape intent directly from Hyprland internals
- Uses `CROSSHAIR_COLOR=#rrggbb`, pywal `color1`, or white fallback
- Draws a `color0` shadow behind the lines, center glyphs, pulses, and label boxes

## Build

Requirements:

- Hyprland headers matching the running Hyprland build
- `g++`
- `make`
- `pkg-config`

Build:

```sh
make
```

Output:

```sh
./crosshair.so
```

Clean:

```sh
make clean
```

## Manual Load

Load:

```sh
hyprctl plugin load "$PWD/crosshair.so"
```

Unload:

```sh
hyprctl plugin unload "$PWD/crosshair.so"
```

Status:

```sh
hyprctl crosshair
hyprctl -j crosshair
```

## hyprpm

After this repo is committed and pushed:

```sh
hyprpm add https://github.com/warpcli/crosshair.git
hyprpm enable crosshair
hyprpm reload
```

For local development, build with `make` and use `hyprctl plugin load` so you can
iterate without waiting on a pushed commit.

## Color

The color is loaded when the plugin starts, in this order:

1. `CROSSHAIR_COLOR=#rrggbb`
2. `~/.cache/wal/colors.json`, using `color1`
3. `~/.cache/wal/colors`, using the second line
4. white fallback

The shadow is loaded from pywal `color0` when available, with black fallback.
`CROSSHAIR_COLOR` only changes the accent color.

Reload the plugin after changing the color source.

## Notes

Hyprland plugins are ABI-bound to Hyprland. Build this plugin against the same
headers as the running compositor. If `hyprpm` reports a header mismatch, run:

```sh
hyprpm update
```
