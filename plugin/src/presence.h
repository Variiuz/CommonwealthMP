#pragma once

#include <string>

void CMP_Presence_Init();
void CMP_Presence_OnPreLoad();
void CMP_Presence_OnGameReady();
void CMP_Presence_Shutdown();
void CMP_Presence_Tick();
void CMP_Presence_Invalidate();
bool CMP_Presence_ReinitDiscord();
std::string CMP_PresenceStatusText();
