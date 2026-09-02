#include "pch.h"
#include "host_launch.h"
#include "session.h"

#include "REX/FModule.h"

#include <filesystem>
#include <vector>

namespace {

namespace fs = std::filesystem;

REX::W32::HANDLE g_serverProcess{ nullptr };

std::string PluginDir()
{
	char path[REX::W32::MAX_PATH]{};
	REX::W32::GetModuleFileNameA(REX::W32::GetCurrentModule(), path, REX::W32::MAX_PATH);
	const fs::path p(path);
	if (p.has_parent_path()) {
		return p.parent_path().string();
	}
	return {};
}

bool FileExists(const std::string& path)
{
	std::error_code ec;
	return !path.empty() && fs::is_regular_file(fs::path(path), ec);
}

void CloseServerProcessHandle()
{
	if (g_serverProcess) {
		REX::W32::CloseHandle(g_serverProcess);
		g_serverProcess = nullptr;
	}
}

bool ProcessAlive(REX::W32::HANDLE a_handle)
{
	if (!a_handle) {
		return false;
	}
	constexpr std::uint32_t kWaitTimeout = 258u;
	return REX::W32::WaitForSingleObject(a_handle, 0) == kWaitTimeout;
}

std::vector<std::string> ServerExeCandidates()
{
	std::vector<std::string> out;
	auto& s = CMP_Session();
	if (!s.settings.serverExe.empty()) {
		out.push_back(s.settings.serverExe);
	}

	const std::string dir = PluginDir();
	if (!dir.empty()) {
		out.push_back((fs::path(dir) / "CommonwealthMP.Server.exe").string());
		out.push_back((fs::path(dir) / "CommonwealthMP.Server-0.6.7.exe").string());
		const fs::path repoServer = fs::path(dir).parent_path().parent_path().parent_path() / "dist" / "server";
		std::error_code ec;
		if (fs::is_directory(repoServer, ec)) {
			for (const auto& entry : fs::directory_iterator(repoServer, ec)) {
				if (!entry.is_regular_file()) {
					continue;
				}
				const auto name = entry.path().filename().string();
				if (name.rfind("CommonwealthMP.Server", 0) == 0 && entry.path().extension() == ".exe") {
					out.push_back(entry.path().string());
				}
			}
		}
	}
	return out;
}

}  // namespace

bool CMP_ServerProcessRunning()
{
	return ProcessAlive(g_serverProcess);
}

bool CMP_LaunchServer(std::uint16_t /*port*/, std::string& errOut)
{
	if (CMP_ServerProcessRunning()) {
		REX::INFO("host: server process already running");
		return true;
	}

	CloseServerProcessHandle();

	std::string exePath;
	for (const auto& candidate : ServerExeCandidates()) {
		if (FileExists(candidate)) {
			exePath = candidate;
			break;
		}
	}
	if (exePath.empty()) {
		errOut = "CommonwealthMP.Server.exe not found (set [Network] ServerExe in CommonwealthMP.ini)";
		return false;
	}

	const fs::path workDir = fs::path(exePath).parent_path();
	std::vector<char> cmdLine(exePath.begin(), exePath.end());
	cmdLine.push_back('\0');

	REX::W32::STARTUPINFOA si{};
	si.size = sizeof(si);
	REX::W32::PROCESS_INFORMATION pi{};

	const bool ok = REX::W32::CreateProcessA(
		exePath.c_str(),
		cmdLine.data(),
		nullptr,
		nullptr,
		false,
		REX::W32::CREATE_NO_WINDOW,
		nullptr,
		workDir.string().c_str(),
		&si,
		&pi);
	if (!ok) {
		errOut = "CreateProcess failed for " + exePath;
		return false;
	}

	REX::W32::CloseHandle(pi.thread);
	g_serverProcess = pi.process;
	REX::INFO("host: launched server {}", exePath);
	return true;
}
