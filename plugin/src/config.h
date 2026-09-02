#pragma once

#include <cstdint>
#include <string>

void CMP_LoadSettings();
void CMP_SaveNetworkSettings(const std::string& host, std::uint16_t port);
void CMP_SavePlayerName(const std::string& name);
void CMP_SavePassword(const std::string& password);
