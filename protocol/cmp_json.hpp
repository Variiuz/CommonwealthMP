#pragma once

#include <cstdlib>
#include <string>
#include <string_view>

namespace cmp {

inline std::string json_quoted(const std::string& text, const char* key)
{
	if (!key) {
		return {};
	}
	const auto needle = std::string("\"") + key + "\"";
	const auto at = text.find(needle);
	if (at == std::string::npos) {
		return {};
	}
	const auto colon = text.find(':', at + needle.size());
	if (colon == std::string::npos) {
		return {};
	}
	const auto q1 = text.find('"', colon);
	if (q1 == std::string::npos) {
		return {};
	}
	const auto q2 = text.find('"', q1 + 1);
	if (q2 == std::string::npos) {
		return {};
	}
	return text.substr(q1 + 1, q2 - q1 - 1);
}

inline double json_number(const std::string& text, const char* key, double fallback)
{
	if (!key) {
		return fallback;
	}
	const auto needle = std::string("\"") + key + "\"";
	const auto at = text.find(needle);
	if (at == std::string::npos) {
		return fallback;
	}
	const auto colon = text.find(':', at + needle.size());
	if (colon == std::string::npos) {
		return fallback;
	}
	const char* start = text.c_str() + colon + 1;
	while (*start == ' ' || *start == '\t' || *start == '\n' || *start == '\r') {
		++start;
	}
	if (*start == '\0' || *start == ',' || *start == '}') {
		return fallback;
	}
	char* end = nullptr;
	const auto v = std::strtod(start, &end);
	if (!end || end == start) {
		return fallback;
	}
	return v;
}

}  // namespace cmp
