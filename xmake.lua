-- Bonds of Skyrim - BondsOfSkyrim.dll
-- Copyright (C) 2026 nozarashi001
-- Licensed under the GNU General Public License, version 3 or (at your option) any later version. See LICENSE.
--
-- How to build: see README.md (xmake 3.x, Visual Studio 2022 C++ build tools, CommonLibSSE-NG 7.5.4).
set_xmakever("3.0.0")

-- CommonLibSSE-NG 7.5.4: https://github.com/alandtse/CommonLibVR
-- exact commit used: c5424463bba9af0d75cde8640ba7ddd4cacb9e39 ("chore(release): 7.5.4"), not modified.
-- Put it in the folder lib\commonlibsse-ng next to this file.
set_config("skyrim_vr", false)
includes("lib/commonlibsse-ng")

set_project("BondsOfSkyrim")
set_version("0.7.0")
set_languages("c++23")
set_warnings("allextra")
add_defines("UNICODE", "_UNICODE")
add_rules("mode.release")

target("BondsOfSkyrim")
    add_deps("commonlibsse-ng")
    add_rules("commonlibsse-ng.plugin", {
        name = "BondsOfSkyrim",
        author = "nozarashi001",
        description = "Amorous Lite: per-NPC values kept in the save, interaction menu (PrismaUI)"
    })
    add_files("src/**.cpp")
    add_headerfiles("src/**.h")
    add_includedirs("src")
    set_pcxxheader("src/pch.h")
