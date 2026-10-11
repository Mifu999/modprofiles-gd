# 2.2.0

## Changed
- The vault is now sorted into one sub-folder per setup instead of one flat pile. A parked file goes into the folder named after the profile it was last in use under, so `mod-profiles-vault/speedrun-setup/` reads as "what that setup holds while it is not running". Files parked before any profile was applied land in `unsorted/`.
- Each file is still stored only once: a folder per profile containing every mod of that profile would duplicate every mod two profiles share.
- `index.json` gained a format version and now records the folder and the parking date for each file. Vaults written by 2.1.0 are read as-is, and their files are sorted into folders on the next switch.
- The files on disk win over the index, so moving things around by hand no longer desynchronises anything.
- The Vault popup lists what each folder holds, by profile name.
- Empty vault folders are tidied up after a switch.

# 2.1.0

## Added
- **Real setup switching.** Applying a profile now moves the `.geode` files themselves: everything the profile does not list leaves `geode/mods` for `geode/mod-profiles-vault`, and everything it does list comes back from there. Your mod list ends up holding exactly the profile instead of a long list of disabled mods. Turn `Switch mod files` off in the settings to go back to toggling only.
- **Vault** button in the profile list: says how many files are parked and puts them all back in one go.
- Dependencies of the mods a profile keeps are kept as well, so a switch cannot leave a mod behind without what it needs.
- The confirmation now says how many files will move, instead of only how many mods are wanted.
- Parked mods stay visible in the profile editor, marked *in the vault*, and are recorded in new profiles - a switch no longer makes them invisible to the next profile you save.

## Changed
- Exporting with files also packs mods that are currently parked
- Importing no longer re-downloads a mod that is sitting in the vault
- `Restore all` leaves a parked file alone when a copy is already installed, instead of overwriting the newer one
- The pack manifest now marks a mod as packed only when its file really made it into the pack

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
