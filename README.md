# Bonds of Skyrim — SKSE plugin source (BondsOfSkyrim.dll)

Source code of `BondsOfSkyrim.dll`, the SKSE plugin of the Skyrim Special Edition mod
[Bonds of Skyrim](https://www.nexusmods.com/skyrimspecialedition/mods/198013)
(Sims-like social interactions with the people of Skyrim).

Copyright (C) 2026 nozarashi001.

## License
This program is free software: you can redistribute it and/or modify it under the terms of the GNU General Public
License as published by the Free Software Foundation, either version 3 of the License, or (at your option) any later
version. It is distributed WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A
PARTICULAR PURPOSE. See the GNU General Public License in [LICENSE](LICENSE).

The DLL is built with [CommonLibSSE-NG](https://github.com/alandtse/CommonLibVR) (GPL-3.0-or-later WITH Modding
Exception AND GPL-3.0 Linking Exception).

`src/PrismaUI_API.h` is the public API header provided by the PrismaUI author for mods to include, unchanged.

This repository covers the DLL only. The rest of the mod (plugin, scripts, menu, texts) is distributed on Nexus Mods
under the permissions shown on the mod page.

## What the plugin does
- keeps per-NPC values of the mod in the SKSE co-save (cooldowns, weariness...);
- the Papyrus functions of the script `AML_Native` (`GetValue`, `SetValue`, `SetRoll`, `OpenUI`...);
- the bridge to the interaction menu page shown with PrismaUI.

## How to build (Windows)
1. Install Visual Studio 2022 (or its Build Tools) with "Desktop development with C++".
2. Install [xmake](https://xmake.io) 3.x.
3. Download CommonLibSSE-NG at commit `c5424463bba9af0d75cde8640ba7ddd4cacb9e39` (release 7.5.4) from
   https://github.com/alandtse/CommonLibVR and put it, unchanged, in `lib/commonlibsse-ng` next to `xmake.lua`.
4. In this folder:
   ```
   xmake f -m release -y
   xmake build -y
   ```
   The DLL is written to `build/windows/x64/release/BondsOfSkyrim.dll`.
