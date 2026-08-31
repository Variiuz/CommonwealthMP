#pragma once

#include <cstdint>
#include <fstream>
#include <string>

#include "cmp_protocol.hpp"

struct ServerConfig {
	std::string name{ "CommonwealthMP" };
	std::string motd;
	std::uint16_t port{ cmp::kDefaultPort };
	bool fake{ true };
	int maxPlayers{ 8 };
	bool verbose{ false };
	bool quiet{ false };
	bool jsonLog{ false };
	float interestUu{ 20000.0f };
	std::string logFile;
	std::string sessionDir;
	std::string configPath;
	bool resetSession{ false };
};

inline std::string trim_ini(std::string s)
{
	while (!s.empty() && (s.front() == ' ' || s.front() == '\t' || s.front() == '\r')) {
		s.erase(s.begin());
	}
	while (!s.empty() && (s.back() == ' ' || s.back() == '\t' || s.back() == '\r')) {
		s.pop_back();
	}
	return s;
}

inline bool parse_bool_ini(const std::string& v, bool fallback)
{
	if (v.empty()) {
		return fallback;
	}
	if (v == "1" || v == "true" || v == "True" || v == "TRUE" || v == "yes" || v == "on") {
		return true;
	}
	if (v == "0" || v == "false" || v == "False" || v == "FALSE" || v == "no" || v == "off") {
		return false;
	}
	return fallback;
}

inline bool write_default_server_ini(const std::string& path)
{
	std::ofstream out(path, std::ios::out | std::ios::trunc);
	if (!out) {
		return false;
	}
	out << "# CommonwealthMP.Server settings.\n"
		<< "\n"
		<< "name=CommonwealthMP\n"
		<< "motd=\n"
		<< "port=7777\n"
		<< "fake=1\n"
		<< "max_players=8\n"
		<< "interest_uu=20000\n"
		<< "verbose=0\n"
		<< "quiet=0\n"
		<< "json_log=0\n"
		<< "# log_file=\n"
		<< "# session_dir=\n";
	return static_cast<bool>(out);
}

inline bool load_server_ini(const std::string& path, ServerConfig& cfg)
{
	std::ifstream in(path);
	if (!in) {
		return false;
	}
	std::string line;
	while (std::getline(in, line)) {
		line = trim_ini(line);
		if (line.empty() || line[0] == '#' || line[0] == ';') {
			continue;
		}
		const auto eq = line.find('=');
		if (eq == std::string::npos) {
			continue;
		}
		const auto key = trim_ini(line.substr(0, eq));
		const auto val = trim_ini(line.substr(eq + 1));
		if (key == "name") {
			cfg.name = val.empty() ? cfg.name : val;
		} else if (key == "motd") {
			cfg.motd = val;
		} else if (key == "port") {
			try {
				cfg.port = static_cast<std::uint16_t>(std::stoi(val));
			} catch (...) {
			}
		} else if (key == "fake") {
			cfg.fake = parse_bool_ini(val, cfg.fake);
		} else if (key == "max_players") {
			try {
				cfg.maxPlayers = std::stoi(val);
			} catch (...) {
			}
		} else if (key == "verbose") {
			cfg.verbose = parse_bool_ini(val, cfg.verbose);
		} else if (key == "quiet") {
			cfg.quiet = parse_bool_ini(val, cfg.quiet);
		} else if (key == "json_log") {
			cfg.jsonLog = parse_bool_ini(val, cfg.jsonLog);
		} else if (key == "interest_uu") {
			try {
				cfg.interestUu = std::stof(val);
			} catch (...) {
			}
		} else if (key == "log_file") {
			cfg.logFile = val;
		} else if (key == "session_dir") {
			cfg.sessionDir = val;
		}
	}
	if (cfg.maxPlayers < 1) {
		cfg.maxPlayers = 1;
	}
	if (cfg.interestUu < 0.0f) {
		cfg.interestUu = 0.0f;
	}
	return true;
}

// Load path; if missing, write defaults then load. createdOut is set when a new file was written.
inline bool ensure_server_ini(const std::string& path, ServerConfig& cfg, bool* createdOut = nullptr)
{
	if (createdOut) {
		*createdOut = false;
	}
	{
		std::ifstream probe(path);
		if (probe) {
			return load_server_ini(path, cfg);
		}
	}
	if (!write_default_server_ini(path)) {
		return false;
	}
	if (createdOut) {
		*createdOut = true;
	}
	return load_server_ini(path, cfg);
}
