# ILL Viewer

A Geode mod for Geometry Dash that adds an in-game viewer for the Impossible Level List (ILL), accessible as a blue "Extreme Demon" style button in the level search menu.

## How it works

- On load, the mod fetches the current ILL list directly from the live API at `api.impossiblelevels.com`, paginating through every page until the full list is retrieved.
- The fetch runs asynchronously in the background and refreshes automatically on every launch — there's no bundled data file to update manually.
- Clicking the ILL button opens Geometry Dash's own level browser, filtered to exactly the levels on the list, 10 per page (matching GD's native pagination).
- If the live fetch fails (e.g. no internet), the mod keeps whatever list it already had in memory rather than clearing it, and shows a clear in-game message.

## Requirements

- An internet connection to fetch the list. Viewing the actual levels also requires internet, since Geometry Dash itself needs to contact its own servers to load level details for any search — this is true of the vanilla game and isn't something this mod can change.

## Building locally

```
geode build
```

Requires the Geode CLI and SDK installed, matching the `"geode"` version declared in `mod.json`.

## Building on GitHub

Pushing to any branch triggers `.github/workflows/build.yml`, which builds Windows and macOS binaries via the official `geode-sdk/build-geode-mod` action and uploads them as workflow artifacts. Tagged releases also attach the built `.geode` files directly for easy download.
