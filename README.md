# Mod Profiles

<img src="logo.png" width="150" alt="Mod Profiles logo" />

Save your whole Geometry Dash mod setup as a profile, switch between setups in two taps, and share one as a single file.

Built for **Geode 5.9.0** / **GD 2.2081**, for Windows, macOS, Android and iOS.

This is an updated merge of two no longer maintained mods:

| Mod | What was taken from it |
|---|---|
| [BlueToadMakerr/Mod-profiles](https://github.com/BlueToadMakerr/Mod-profiles) | saving and restoring enabled/disabled states, the main-menu entry point |
| [LimeGradient/ModProfiles](https://github.com/LimeGradient/ModProfiles) | shareable profile packs, with index / packed / remote mod sources |

## Features

- **Profiles** - a named set of mod states, stored as one JSON file each in the mod's save directory
- **Auto backup** - the state before an apply is always kept, so a wrong switch is reversible
- **Editor** - per-mod toggles with search, *All / None / Invert*, and *Snapshot* to copy your current state
- **Sharing** - export to a `.modprofile` file, either list-only or with the `.geode` files embedded
- **Importing** - installs the packed mods, downloads the missing ones from the Geode index, then applies
- **Non-destructive** - mods are only enabled and disabled; nothing is ever deleted

## Pack format

A `.modprofile` is a zip:

```
profile.json        the profile (same shape as a local profile file)
mods/<id>.geode     optional, for mods that are not on the index
```

```json
{
    "format": 1,
    "name": "Speedrun setup",
    "slug": "speedrun-setup",
    "description": "What I run for practice",
    "creator": "Mifuu",
    "version": "1.0.0",
    "created": 1760000000,
    "mods": {
        "geode.node-ids":  { "enabled": true,  "source": "index",  "version": "1.23.3", "name": "Node IDs" },
        "me.private-mod":  { "enabled": true,  "source": "packed", "version": "1.0.0",  "name": "Private Mod" },
        "someone.hosted":  { "enabled": false, "source": "remote", "url": "https://example.com/mod.geode", "name": "Hosted Mod" }
    }
}
```

`source` says where a mod can be obtained when it is not installed: `index` (the Geode index), `packed` (shipped inside this file), `remote` (an explicit URL) or `unknown` (the index is tried anyway).

## Building

```sh
# with the Geode CLI set up
geode build
```

The GitHub workflow in `.github/workflows/build.yml` builds for all five targets and uploads the combined `.geode`.

## Layout

```
src/
├── Profile.hpp/.cpp    data model + JSON (de)serialisation
├── Manager.hpp/.cpp    the profile collection on disk, and applying one
├── Packs.hpp/.cpp      .modprofile reading/writing, index lookups, downloads
├── main.cpp            load-time setup + the MenuLayer button
└── ui/
    ├── ProfilesPopup.hpp/.cpp      the profile list and its actions
    └── ProfileEditPopup.hpp/.cpp   per-mod toggles for one profile
```
