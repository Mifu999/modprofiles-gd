# 2.0.0

A rewrite for Geode 5, merging two earlier mods into one maintained mod.

## Added
- Shareable `.modprofile` packs, with a choice between a list-only file and one carrying the `.geode` files
- Importing a pack installs the mods it ships and downloads the missing ones from the Geode index
- Automatic backup of your setup before any profile is applied
- Search, *All / None / Invert* and *Snapshot* in the profile editor
- Mods a profile lists but that are not installed are kept and shown instead of being dropped on save
- Settings: automatic backup, disabling unlisted mods, allowing self-disable, downloading missing mods

## Changed
- Profiles are one readable JSON file each in the mod's save directory, instead of flat saved values
- Nothing is ever deleted on import; unlisted mods are disabled at most, and only if asked for
- Downloads follow the `download_link` the Geode index reports rather than a guessed URL

## Fixed
- The main menu button no longer crashes the game when another mod replaced the bottom menu
- The profile list no longer frees a button while that button's own callback is running
- Export writes `.geode` files under deterministic names, so importing finds them again
