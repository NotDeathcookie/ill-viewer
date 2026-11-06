# ILL Viewer

A Geode mod for Geometry Dash that adds an in-game viewer for the Impossible Level List (ILL).

## Recovery notes

This project was reconstructed from a saved AI chat transcript after the original
`GeodeMods/ill viewer` folder was deleted. Status of each file:

- **`main.cpp`** — fully recovered. This is the final, working version from the
  transcript: JSON loader, manual 10-item pagination through `LevelBrowserLayer`,
  and the blue-tinted Extreme Demon button on `LevelSearchLayer`.
- **`CMakeLists.txt`** — fully recovered.
- **`mod.json`** — fully recovered.
- **`resources/ill_data.json`** — **NOT fully recovered.** The transcript only ever
  showed a 2-entry *structure example*, never the full ~1,981-level dataset (it was
  too large to paste into chat). The file here is just that example so the project
  builds. Replace it with your real data — regenerate it with your
  `ILL_data_grabber.py` script against the ILL API (`api.impossiblelevels.com`), or
  restore it from wherever you last saved its output.
- **`resources/ill_icon.png`** — **not recoverable from the transcript** (it's a
  binary image, chat logs are text-only). If you still have the PNG anywhere
  (Downloads, Discord, email, a backup), drop an 80x80 transparent PNG at
  `resources/ill_icon.png`. Until then the code falls back to the built-in
  Extreme Demon sprite, so the mod still builds and runs without it.

## Building locally

```
geode build
```

## Building on GitHub

Pushing to any branch triggers `.github/workflows/build.yml`, which builds
Windows and macOS binaries via the official `geode-sdk/build-geode-mod` action
and uploads them as workflow artifacts.
