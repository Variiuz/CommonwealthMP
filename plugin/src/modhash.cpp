#include "pch.h"
#include "modhash.h"

#include <algorithm>
#include <cctype>
#include <string>
#include <vector>

namespace {

std::string LowerCopy(std::string s)
{
	for (char& c : s) {
		c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
	}
	return s;
}

std::uint32_t Fnv1aAppend(std::uint32_t h, const char* data, std::size_t len)
{
	for (std::size_t i = 0; i < len; ++i) {
		h ^= static_cast<unsigned char>(data[i]);
		h *= 16777619u;
	}
	return h;
}

}  // namespace

std::uint32_t CMP_ComputeModHash()
{
	auto* data = RE::TESDataHandler::GetSingleton();
	if (!data) {
		return 0;
	}

	std::vector<std::string> names;
	for (auto& file : data->compiledFileCollection.files) {
		if (!file || !file->filename[0]) {
			continue;
		}
		names.push_back(LowerCopy(file->filename));
	}
	for (auto& file : data->compiledFileCollection.smallFiles) {
		if (!file || !file->filename[0]) {
			continue;
		}
		names.push_back(LowerCopy(file->filename));
	}
	std::sort(names.begin(), names.end());
	names.erase(std::unique(names.begin(), names.end()), names.end());

	std::uint32_t h = 2166136261u;
	for (const auto& name : names) {
		h = Fnv1aAppend(h, name.data(), name.size());
		h = Fnv1aAppend(h, "\n", 1);
	}
	return h;
}
