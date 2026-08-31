#pragma once

#include <cctype>
#include <string>
#include <string_view>

namespace cmp {

inline constexpr std::size_t kPlayerKeyMax = 31;

inline std::string sanitize_player_key(std::string_view raw, std::string_view emptyFallback = {})
{
	std::string out;
	for (unsigned char c : raw) {
		if (std::isalnum(c) || c == '-' || c == '_') {
			out.push_back(static_cast<char>(c));
		}
	}
	if (out.empty()) {
		return std::string(emptyFallback);
	}
	if (out.size() > kPlayerKeyMax) {
		out.resize(kPlayerKeyMax);
	}
	return out;
}

}  // namespace cmp
