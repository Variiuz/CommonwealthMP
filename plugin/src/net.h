#pragma once

#include "session.h"

#include "cmp_protocol.hpp"

#include <cstdint>
#include <string>

bool CMP_Join(std::string host, std::uint16_t port, std::uint8_t flags = 0);
void CMP_Leave();
void CMP_QueryStart(std::string host, std::uint16_t port);
bool CMP_QueryPoll(SessionQueryResult& out);
void CMP_NetPoll();
void CMP_SendLocalPose();
void CMP_SendChat(const char* text);
void CMP_SendKick(std::uint32_t targetPeerId, const char* reason);
void CMP_SendTeleport(std::uint32_t targetPeerId);
void CMP_Print(const std::string& line);
std::string CMP_StatusText();
void CMP_ApplyWorldSnapshot(const cmp::WorldSnapshot& snap);
bool CMP_PlayerInCommonwealth();
void CMP_EnsureCommonwealthExterior();

float CMP_EffectiveInterpDelayMs();
void CMP_Net_Send(const void* data, int len);
