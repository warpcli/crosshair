# wayland-crosshair

A lightweight Wayland overlay that draws a CAD-style crosshair at the current
cursor position.

It creates transparent, click-through `wlr-layer-shell` overlay surfaces on each
monitor, then draws dashed horizontal and vertical Cairo lines only on the
monitor containing the cursor. The cursor position is read from Hyprland.

## Features

- Full-screen crosshair following the pointer
- Multi-monitor aware using global monitor geometry
- Click-through overlay; it does not steal mouse or keyboard focus
- About 120 Hz cursor sampling by default
- Narrow damage redraws instead of repainting entire monitors on every cursor move
- Uses the pywal accent color and updates live when colors change
- Optional `wlsunset` gamma boost while the crosshair is running

## Requirements

Runtime:

- Wayland compositor with `wlr-layer-shell`
- Hyprland, for `hyprctl cursorpos`
- GTK 3
- gtk-layer-shell
- Cairo
- `wlsunset`, optional

Build:

- `gcc`
- `make`
- `pkg-config`
- GTK 3 development files
- gtk-layer-shell development files
- Cairo development files

Arch:

```sh
sudo pacman -S gcc make pkgconf gtk3 gtk-layer-shell cairo
```

Ubuntu:

```sh
sudo apt-get install build-essential pkg-config libgtk-3-dev libgtk-layer-shell-dev libcairo2-dev
```

## Build

```sh
make
```

The binary is written to:

```sh
./crosshair
```

Clean build output:

```sh
make clean
```

## Usage

Run the overlay:

```sh
./crosshair
```

Run with a temporary gamma boost through `wlsunset`:

```sh
./crosshair 1.2
```

Stop it:

```sh
pkill crosshair
```

## Color

The crosshair color is loaded in this order:

1. `CROSSHAIR_COLOR=#rrggbb`
2. `~/.cache/wal/colors.json`, using `colors.color1`
3. `~/.cache/wal/colors`, using the second line
4. white fallback

Examples:

```sh
CROSSHAIR_COLOR=#ff005f ./crosshair
```

When `CROSSHAIR_COLOR` is not set, the app watches both pywal files and reloads
the color live after theme changes.

## Release

GitHub Actions builds release artifacts when a tag matching `v*` is pushed:

```sh
git tag v0.1.0
git push origin v0.1.0
```

The release contains:

- `wayland-crosshair-linux-x86_64.tar.gz`
- `wayland-crosshair-linux-aarch64.tar.gz`
- `checksums.txt`

The published binary is dynamically linked. Target machines still need the
runtime libraries listed above.

## Notes

This project currently keeps GTK because GTK plus gtk-layer-shell provides the
Wayland windowing and layer-shell plumbing. Cairo only draws pixels; it does not
create a Wayland overlay by itself.
