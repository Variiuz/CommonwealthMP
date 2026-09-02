#pragma once

#include <cstdint>
#include <string>

bool CMP_LaunchServer(std::uint16_t port, std::string& errOut);
bool CMP_ServerProcessRunning();
