-- include subprojects
includes("lib/commonlibf4")

set_project("CommonwealthMP")
set_version("0.6.7") -- x-release-please-version
set_license("GPL-3.0")
set_languages("c++23")
set_warnings("allextra")

add_rules("mode.debug", "mode.releasedbg")

add_requires("imgui", { configs = { dx11 = true, win32 = true } })
add_requires("rapidjson")

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
	add_files(
		"lib/discord-rpc/src/discord_rpc.cpp",
		"lib/discord-rpc/src/connection_win.cpp",
		"lib/discord-rpc/src/discord_register_win.cpp",
		"lib/discord-rpc/src/rpc_connection.cpp",
		"lib/discord-rpc/src/serialization.cpp"
	)
	add_headerfiles("src/**.h")
	add_includedirs("src", "../protocol", "lib/discord-rpc/include")
	add_packages("imgui", "rapidjson")
	add_defines("DISCORD_WINDOWS", 'CMP_DISCORD_APP_ID="1544466839742971924"', 'CMP_DISCORD_IMAGE_KEY="cmp_logo1"')
	add_syslinks("ws2_32", "dbghelp", "psapi", "user32", "d3d11", "dxgi", "d3dcompiler", "advapi32")
	set_pcxxheader("src/pch.h")
	add_includedirs("gens")

	on_config(function (target)
		target:add("installfiles", path.join(os.scriptdir(), "..", "data", "F4SE", "Plugins", "CommonwealthMP.ini"), { prefixdir = "F4SE/Plugins" })
		-- Git metadata in a generated header (not target-wide defines) so
		-- configure / stamp updates do not dirty every translation unit.
		local git = os.iorun("git describe --tags --always --dirty")
		if git and git ~= "" then
			git = git:gsub("^%s+", ""):gsub("%s+$", "")
		else
			git = "unknown"
		end
		local meta_dir = path.join(os.scriptdir(), "gens")
		local meta = path.join(meta_dir, "cmp_build_meta.h")
		local content = "#pragma once\n#define CMP_GIT_VERSION \"" .. git .. "\"\n"
		os.mkdir(meta_dir)
		local old = ""
		if os.isfile(meta) then
			old = io.readfile(meta) or ""
		end
		if old ~= content then
			io.writefile(meta, content)
		end
	end)
