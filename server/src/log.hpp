#pragma once

#include <chrono>
#include <cctype>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <fstream>
#include <mutex>
#include <string>

#ifdef _WIN32
#include <Windows.h>
#endif

enum class LogLevel {
	Debug = 0,
	Info = 1,
	Warn = 2,
	Error = 3
};

class ServerLog {
public:
	static ServerLog& instance()
	{
		static ServerLog s;
		return s;
	}

	void ensure_console()
	{
#ifdef _WIN32
		const bool allocated = !GetConsoleWindow();
		if (allocated) {
			AllocConsole();
			FILE* fp = nullptr;
			freopen_s(&fp, "CONOUT$", "w", stdout);
			freopen_s(&fp, "CONOUT$", "w", stderr);
			freopen_s(&fp, "CONIN$", "r", stdin);
		}
		setvbuf(stdout, nullptr, _IONBF, 0);
		setvbuf(stderr, nullptr, _IONBF, 0);
		consoleOut_ = GetStdHandle(STD_OUTPUT_HANDLE);
		if (HANDLE input = GetStdHandle(STD_INPUT_HANDLE); input && input != INVALID_HANDLE_VALUE) {
			DWORD mode = 0;
			if (GetConsoleMode(input, &mode)) {
				mode &= ~ENABLE_QUICK_EDIT_MODE;
				mode |= ENABLE_EXTENDED_FLAGS;
				SetConsoleMode(input, mode);
			}
		}
		vtOut_ = enable_vt_(STD_OUTPUT_HANDLE);
		enable_vt_(STD_ERROR_HANDLE);
		if (consoleOut_ && consoleOut_ != INVALID_HANDLE_VALUE) {
			CONSOLE_SCREEN_BUFFER_INFO info{};
			if (GetConsoleScreenBufferInfo(consoleOut_, &info)) {
				defaultAttr_ = info.wAttributes;
			}
		}
		if (HWND wnd = GetConsoleWindow()) {
			ShowWindow(wnd, SW_SHOW);
		}
#endif
		titlePrefix_ = "CMP";
		refresh_title_unlocked();
	}

	void set_title(const std::string& title)
	{
		std::lock_guard lock(mutex_);
		titlePrefix_ = title.empty() ? "CMP" : title;
		refresh_title_unlocked();
	}

	bool open_file(const std::string& path)
	{
		std::lock_guard lock(mutex_);
		flush_file_unlocked();
		file_.close();
		file_.open(path, std::ios::out | std::ios::app);
		path_ = path;
		return file_.is_open();
	}

	void set_level(LogLevel consoleLevel, LogLevel fileLevel)
	{
		consoleLevel_ = consoleLevel;
		fileLevel_ = fileLevel;
	}

	void set_status_enabled(bool enabled)
	{
		std::lock_guard lock(mutex_);
		statusEnabled_ = enabled;
		if (!enabled) {
			statusLine_.clear();
		}
		refresh_title_unlocked();
	}

	void set_status(std::string line)
	{
		std::lock_guard lock(mutex_);
		if (line.size() > 180) {
			line.resize(180);
		}
		statusLine_ = std::move(line);
		if (statusEnabled_) {
			refresh_title_unlocked();
		}
	}

	void flush()
	{
		std::lock_guard lock(mutex_);
		flush_file_unlocked();
	}

	void write(LogLevel level, const char* fmt, ...)
	{
		char body[1024]{};
		va_list args;
		va_start(args, fmt);
		std::vsnprintf(body, sizeof(body), fmt, args);
		va_end(args);

		const auto stampStr = stamp();
		const auto levelStr = level_name(level);
		const auto plain = stampStr + " [" + levelStr + "] " + body + "\n";
		std::lock_guard lock(mutex_);
		if (level >= consoleLevel_) {
			write_console_unlocked(level, stampStr, levelStr, body);
		}
		if (file_.is_open() && level >= fileLevel_) {
			file_ << plain;
			pending_++;
			if (level >= LogLevel::Info || pending_ >= 64) {
				flush_file_unlocked();
			} else {
				maybe_flush_timed_unlocked();
			}
		}
	}

	void write_raw(const char* text)
	{
		if (!text) {
			return;
		}
		std::lock_guard lock(mutex_);
		write_text_unlocked(text, Part::Plain);
	}

	void write_banner(const char* text)
	{
		if (!text) {
			return;
		}
		std::lock_guard lock(mutex_);
		int line = 0;
		const char* p = text;
		while (*p) {
			const char* start = p;
			while (*p && *p != '\n') {
				++p;
			}
			if (p > start) {
				Part part = Part::Banner;
				if (line == 0) {
					part = Part::BannerGold;
				} else if (line >= 4) {
					part = Part::BannerAccent;
				}
				write_slice_unlocked(start, static_cast<std::size_t>(p - start), part);
			}
			if (*p == '\n') {
				write_text_unlocked("\n", Part::Plain);
				++p;
				++line;
			}
		}
		restore_default_unlocked();
	}

	const std::string& path() const { return path_; }

private:
	enum class Part {
		Plain,
		Dim,
		Timestamp,
		LevelDebug,
		LevelInfo,
		LevelWarn,
		LevelError,
		Message,
		MetaKey,
		MetaValue,
		Path,
		Number,
		Emphasis,
		ErrorText,
		BannerGold,
		Banner,
		BannerAccent
	};

#ifdef _WIN32
	static bool enable_vt_(DWORD stdHandle)
	{
		HANDLE out = GetStdHandle(stdHandle);
		if (!out || out == INVALID_HANDLE_VALUE) {
			return false;
		}
		DWORD mode = 0;
		if (!GetConsoleMode(out, &mode)) {
			return false;
		}
		mode |= ENABLE_VIRTUAL_TERMINAL_PROCESSING;
		return SetConsoleMode(out, mode) != 0;
	}

	static WORD part_attr_(Part part, WORD defaultAttr)
	{
		const WORD bg = defaultAttr & 0xF0;
		switch (part) {
		case Part::Plain:
		case Part::Message:
			return bg | FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE;
		case Part::Dim:
		case Part::Timestamp:
			return bg | FOREGROUND_INTENSITY;
		case Part::LevelDebug:
			return bg | FOREGROUND_BLUE | FOREGROUND_INTENSITY;
		case Part::LevelInfo:
			return bg | FOREGROUND_GREEN | FOREGROUND_BLUE | FOREGROUND_INTENSITY;
		case Part::LevelWarn:
			return bg | FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_INTENSITY;
		case Part::LevelError:
			return bg | FOREGROUND_RED | FOREGROUND_INTENSITY;
		case Part::MetaKey:
			return bg | FOREGROUND_GREEN | FOREGROUND_INTENSITY;
		case Part::MetaValue:
			return bg | FOREGROUND_RED | FOREGROUND_BLUE | FOREGROUND_INTENSITY;
		case Part::Path:
			return bg | FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE | FOREGROUND_INTENSITY;
		case Part::Number:
			return bg | FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_INTENSITY;
		case Part::Emphasis:
			return bg | FOREGROUND_GREEN | FOREGROUND_BLUE | FOREGROUND_INTENSITY;
		case Part::ErrorText:
			return bg | FOREGROUND_RED | FOREGROUND_INTENSITY;
		case Part::BannerGold:
			return bg | FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_INTENSITY;
		case Part::Banner:
			return bg | FOREGROUND_GREEN | FOREGROUND_BLUE | FOREGROUND_INTENSITY;
		case Part::BannerAccent:
			return bg | FOREGROUND_RED | FOREGROUND_BLUE | FOREGROUND_INTENSITY;
		}
		return defaultAttr;
	}
#endif

	static const char* vt_part_(Part part)
	{
		switch (part) {
		case Part::Plain:
		case Part::Message:
			return "\x1b[37m";
		case Part::Dim:
		case Part::Timestamp:
			return "\x1b[90m";
		case Part::LevelDebug:
			return "\x1b[94m";
		case Part::LevelInfo:
			return "\x1b[96m";
		case Part::LevelWarn:
			return "\x1b[93m";
		case Part::LevelError:
			return "\x1b[91m";
		case Part::MetaKey:
			return "\x1b[92m";
		case Part::MetaValue:
			return "\x1b[95m";
		case Part::Path:
			return "\x1b[97m";
		case Part::Number:
			return "\x1b[33m";
		case Part::Emphasis:
			return "\x1b[96m";
		case Part::ErrorText:
			return "\x1b[91m";
		case Part::BannerGold:
			return "\x1b[93m";
		case Part::Banner:
			return "\x1b[96m";
		case Part::BannerAccent:
			return "\x1b[95m";
		}
		return "\x1b[0m";
	}

	static Part level_part_(LogLevel level)
	{
		switch (level) {
		case LogLevel::Debug:
			return Part::LevelDebug;
		case LogLevel::Info:
			return Part::LevelInfo;
		case LogLevel::Warn:
			return Part::LevelWarn;
		case LogLevel::Error:
			return Part::LevelError;
		}
		return Part::LevelInfo;
	}

	static const char* level_name(LogLevel level)
	{
		switch (level) {
		case LogLevel::Debug:
			return "DEBUG";
		case LogLevel::Info:
			return "INFO";
		case LogLevel::Warn:
			return "WARN";
		case LogLevel::Error:
			return "ERROR";
		}
		return "?";
	}

	static std::string stamp()
	{
		using clock = std::chrono::system_clock;
		const auto now = clock::now();
		const auto t = clock::to_time_t(now);
		const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()) % 1000;
		std::tm local{};
#ifdef _WIN32
		localtime_s(&local, &t);
#else
		localtime_r(&t, &local);
#endif
		char buf[32]{};
		std::snprintf(
			buf,
			sizeof(buf),
			"%04d-%02d-%02d %02d:%02d:%02d.%03d",
			local.tm_year + 1900,
			local.tm_mon + 1,
			local.tm_mday,
			local.tm_hour,
			local.tm_min,
			local.tm_sec,
			static_cast<int>(ms.count()));
		return buf;
	}

	static bool is_number_token_(const std::string& token)
	{
		if (token.empty()) {
			return false;
		}
		std::size_t i = 0;
		if (token[i] == '-' || token[i] == '+') {
			++i;
		}
		bool sawDigit = false;
		for (; i < token.size(); ++i) {
			const char ch = token[i];
			if (std::isdigit(static_cast<unsigned char>(ch)) != 0) {
				sawDigit = true;
				continue;
			}
			if (ch == '.' && sawDigit) {
				continue;
			}
			return false;
		}
		return sawDigit;
	}

	static bool is_path_token_(const std::string& token)
	{
		return token.find('\\') != std::string::npos
			|| token.find('/') != std::string::npos
			|| token.find(':') != std::string::npos;
	}

	static bool is_error_word_(const std::string& token)
	{
		std::string lower;
		lower.reserve(token.size());
		for (char ch : token) {
			lower.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(ch))));
		}
		return lower == "failed"
			|| lower == "failure"
			|| lower == "error"
			|| lower == "rejected"
			|| lower == "reject"
			|| lower == "denied"
			|| lower == "invalid"
			|| lower == "unknown"
			|| lower == "bad"
			|| lower == "timeout"
			|| lower == "full";
	}

	void flush_file_unlocked()
	{
		if (file_.is_open()) {
			file_.flush();
			pending_ = 0;
			lastFlush_ = std::chrono::steady_clock::now();
		}
	}

	void maybe_flush_timed_unlocked()
	{
		using clock = std::chrono::steady_clock;
		if (pending_ == 0) {
			return;
		}
		if (clock::now() - lastFlush_ >= std::chrono::milliseconds(250)) {
			flush_file_unlocked();
		}
	}

	void refresh_title_unlocked()
	{
#ifdef _WIN32
		std::string title;
		if (statusEnabled_ && !statusLine_.empty()) {
			title = statusLine_;
		} else {
			title = titlePrefix_.empty() ? "CMP" : titlePrefix_;
		}
		if (title.size() > 200) {
			title.resize(200);
		}
		SetConsoleTitleA(title.c_str());
#else
		(void)0;
#endif
	}

	void restore_default_unlocked()
	{
#ifdef _WIN32
		if (consoleOut_ && consoleOut_ != INVALID_HANDLE_VALUE) {
			SetConsoleTextAttribute(consoleOut_, defaultAttr_);
		}
#endif
		if (vtOut_) {
			std::fputs("\x1b[0m", stdout);
			std::fflush(stdout);
		}
	}

	void write_slice_unlocked(const char* text, std::size_t len, Part part)
	{
		if (!text || len == 0) {
			return;
		}
#ifdef _WIN32
		if (consoleOut_ && consoleOut_ != INVALID_HANDLE_VALUE) {
			SetConsoleTextAttribute(consoleOut_, part_attr_(part, defaultAttr_));
			DWORD written = 0;
			WriteConsoleA(consoleOut_, text, static_cast<DWORD>(len), &written, nullptr);
			return;
		}
#endif
		if (vtOut_) {
			std::fputs(vt_part_(part), stdout);
		}
		std::fwrite(text, 1, len, stdout);
	}

	void write_text_unlocked(const char* text, Part part)
	{
		if (!text) {
			return;
		}
		write_slice_unlocked(text, std::strlen(text), part);
	}

	void write_token_unlocked(LogLevel level, const std::string& token, bool first)
	{
		if (!first) {
			write_text_unlocked(" ", Part::Plain);
		}
		if (token.empty()) {
			return;
		}

		const auto eq = token.find('=');
		if (eq != std::string::npos && eq > 0) {
			write_slice_unlocked(token.c_str(), eq + 1, Part::MetaKey);
			write_slice_unlocked(token.c_str() + eq + 1, token.size() - eq - 1, Part::MetaValue);
			return;
		}

		Part part = Part::Message;
		if (level == LogLevel::Error) {
			part = Part::ErrorText;
		} else if (level == LogLevel::Warn && is_error_word_(token)) {
			part = Part::ErrorText;
		} else if (is_path_token_(token)) {
			part = Part::Path;
		} else if (is_number_token_(token)) {
			part = Part::Number;
		} else if (is_error_word_(token)) {
			part = Part::ErrorText;
		} else if (token == "CommonwealthMP.Server" || token == "CommonwealthMP" || token == "CMP1") {
			part = Part::Emphasis;
		}
		write_text_unlocked(token.c_str(), part);
	}

	void write_body_unlocked(LogLevel level, const char* body)
	{
		if (!body || !body[0]) {
			write_text_unlocked("\n", Part::Plain);
			return;
		}

		bool first = true;
		const char* p = body;
		while (*p) {
			while (*p == ' ') {
				++p;
			}
			if (!*p) {
				break;
			}
			const char* start = p;
			while (*p && *p != ' ') {
				++p;
			}
			write_token_unlocked(level, std::string(start, p), first);
			first = false;
		}
		write_text_unlocked("\n", Part::Plain);
	}

	void write_console_unlocked(LogLevel level, const std::string& stampStr, const char* levelStr, const char* body)
	{
		write_text_unlocked(stampStr.c_str(), Part::Timestamp);
		write_text_unlocked(" [", Part::Dim);
		write_text_unlocked(levelStr, level_part_(level));
		write_text_unlocked("] ", Part::Dim);
		write_body_unlocked(level, body);
		restore_default_unlocked();
	}

	std::mutex mutex_;
	std::ofstream file_;
	std::string path_;
	std::string titlePrefix_;
	std::string statusLine_;
	LogLevel consoleLevel_{ LogLevel::Debug };
	LogLevel fileLevel_{ LogLevel::Debug };
	bool vtOut_{ false };
	bool statusEnabled_{ false };
#ifdef _WIN32
	HANDLE consoleOut_{ INVALID_HANDLE_VALUE };
	WORD defaultAttr_{ FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE };
#endif
	int pending_{ 0 };
	std::chrono::steady_clock::time_point lastFlush_{ std::chrono::steady_clock::now() };
};

#define LOG_DEBUG(...) ServerLog::instance().write(LogLevel::Debug, __VA_ARGS__)
#define LOG_INFO(...) ServerLog::instance().write(LogLevel::Info, __VA_ARGS__)
#define LOG_WARN(...) ServerLog::instance().write(LogLevel::Warn, __VA_ARGS__)
#define LOG_ERROR(...) ServerLog::instance().write(LogLevel::Error, __VA_ARGS__)
