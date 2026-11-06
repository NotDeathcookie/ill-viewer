# ILL Viewer

A Geode mod for Geometry Dash that lets you browse the Impossible Level List (ILL) without leaving the game.

## What it does

- Adds a blue-tinted Extreme Demon button to the level search screen. Tap it to open the list.
- Shows the ILL in pages of 10 levels, using the game's normal level browser.
- Loads the full list (about 2,000 levels) from a bundled data file, so it works offline.
- Comes with its own custom icon, so it looks right in your mods list.

## Installing

1. Go to the [Releases page](../../releases) and download the latest `.geode` file for your operating system.
2. Drop it into your Geometry Dash `geode/mods` folder, or install it with Geode.
3. Launch the game. You can find the button on the level search screen.

Built for Geometry Dash **2.2081** with Geode **5.11.2** (may work with other verisons).

## Level data

The list is already included in the mod. If you're curious where it comes from, the data grabber Python file is available in the [ILL Data Grabber repository](https://github.com/NotDeathcookie/impossible-level-list-data-grabber).

## Building it yourself

With the Geode CLI and SDK installed:

    geode build

Or use GitHub. Every push runs `.github/workflows/build.yml`, which builds Windows and macOS versions using the official `geode-sdk/build-geode-mod` action. Build outputs show up in the Actions tab, and finished versions are published on the Releases page.

## Credits

Level data comes from the Impossible Level List.
