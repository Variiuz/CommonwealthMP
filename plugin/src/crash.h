#pragma once

void CMP_InstallCrashHandler();
void CMP_WatchQuit();
void CMP_CrashNote(const char* what);
bool CMP_SehCall(const char* what, void (*fn)(void*), void* ctx);
