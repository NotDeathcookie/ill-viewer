# ILL Viewer

A Geode mod for Geometry Dash that adds an in-game viewer for the Impossible Level List (ILL).

## What's here

`main.cpp` loads the ILL data and adds a viewer with pagination through `LevelBrowserLayer`, plus a blue-tinted Extreme Demon button on `LevelSearchLayer` for quick access.

## A heads-up about the data file

`resources/ill_data.json` in this repo is just a small placeholder — a 2-entry example, not the full ~1,981-level dataset. The project builds fine with it as-is, but the in-game list won't be complete until it's swapped for the real file.

To regenerate it, run `ILL_data_grabber.py` against the ILL API (`api.impossiblelevels.com`).

The mod icon (`resources/ill_icon.png`) is also a placeholder for now — without a real one dropped in, it falls back to the built-in Extreme Demon sprite, which works fine either way.

## Building locally
