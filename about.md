# Mod Profiles

Save your whole mod setup as a **profile**, switch between setups in two taps, and share a setup with someone else as a single file.

## What it does

- **Save current setup** writes down which of your mods are on and which are off.
- **Apply** rebuilds your mods folder to match the profile: the mod files it does not list are moved out to `geode/mod-profiles-vault`, and the ones it wants are moved back in. 270 mods in, 80 in the profile, and your mod list holds those 80. Your current setup is saved as *Auto backup* first, so a wrong switch is always one tap from being undone.
- **Vault** puts every parked file back into `geode/mods` whenever you want.
- **Edit** lets you flip mods on and off inside a profile, with a search box and *All / None / Invert*.
- **Share** writes the profile to a `.modprofile` file. You choose between a tiny list-only file (the other side downloads the mods from the Geode index) or one that carries the `.geode` files along, for mods that are not published.
- **Import** reads such a file, installs what is missing, and applies it.

## What it never does

It never deletes a mod. Files that leave your mods folder are parked in the vault next to it, and *Vault → Restore all* brings every one of them back.

## Credits

This mod merges the ideas of two earlier, no longer maintained mods, rewritten for Geode 5:

- [Mod profiles](https://github.com/BlueToadMakerr/Mod-profiles) by **BlueToadMaker** - saving and loading enabled/disabled states
- [ModProfiles](https://github.com/LimeGradient/ModProfiles) by **LimeGradient** - shareable profile packs with index, packed and remote mods

Thanks also to [@dulakgg](https://github.com/dulakgg), whose *denabler* inspired the original Mod profiles.
