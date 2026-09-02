#pragma once

void CMP_Overlay_Init(void* device, void* context, void* hwnd);
void CMP_Overlay_Shutdown();
void CMP_Overlay_Tick();
void CMP_Overlay_Toggle();
void CMP_Overlay_SetVisible(bool visible);
bool CMP_Overlay_IsVisible();
void CMP_Overlay_Draw(void* renderTargetView);
