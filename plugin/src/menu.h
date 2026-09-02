#pragma once

#include <string>

void CMP_InstallMenu();
void CMP_MenuTick();
void CMP_MenuOnNewGame();
void CMP_MenuOpenJoin(bool titleMenu);
void CMP_MenuOpenHost(bool titleMenu);
void CMP_MenuHost();
void CMP_MenuDisconnect();
bool CMP_MenuIsConnected();
bool CMP_MenuJoinPending();
bool CMP_MenuFormOpen();
void CMP_MenuDrawForms();
bool CMP_MenuHostJoinPending();
void CMP_MenuHostJoinAfterLoad();
std::string CMP_MenuPresencePhase(bool& menuActive);
std::string CMP_VersionStamp();
void CMP_StripLocalGear();
