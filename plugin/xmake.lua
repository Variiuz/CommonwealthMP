-- include subprojects
includes("lib/commonlibf4")

set_project("CommonwealthMP")
set_version("0.5.7")
set_license("GPL-3.0")
set_languages("c++23")
set_warnings("allextra")

add_rules("mode.debug", "mode.releasedbg")
add_rules("plugin.vsxmake.autoupdate")

target("CommonwealthMP")
	set_values("xse.plugin.name", "CommonwealthMP")
	set_values("xse.plugin.author", "CommonwealthMP")
	set_values("xse.plugin.description", "Fallout 4 coop over a dedicated server.")

	add_rules("commonlibf4.plugin", {
		name = "CommonwealthMP",
		author = "CommonwealthMP",
		description = "Fallout 4 coop over a dedicated server."
	})

	add_files("src/**.cpp|src/udp_win.cpp")
	add_files("src/udp_win.cpp", { pch = false })
	add_headerfiles("src/**.h")
	add_includedirs("src", "../protocol")
	add_syslinks("ws2_32", "dbghelp", "psapi", "user32")
	set_pcxxheader("src/pch.h")

	on_config(function (target)
		target:add("installfiles", path.join(os.scriptdir(), "..", "data", "F4SE", "Plugins", "CommonwealthMP.ini"), { prefixdir = "F4SE/Plugins" })
		target:add("defines", 'CMP_BUILD_STAMP="' .. os.date("%Y-%m-%dT%H:%M") .. '"')
	end)
