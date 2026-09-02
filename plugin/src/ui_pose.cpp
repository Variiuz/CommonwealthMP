#include "pch.h"
#include "ui_pose.h"
#include "indicators.h"
#include "cmp_protocol.hpp"

#include <cstring>

namespace {

std::uint32_t g_localMenuFlags{ 0 };

}  // namespace

void CMP_OnMenuOpenClose(const char* menuName, bool opening)
{
	if (!menuName) {
		return;
	}
	if (std::strcmp(menuName, "PipboyMenu") == 0) {
		if (opening) {
			g_localMenuFlags |= cmp::PoseFlag::Pipboy;
			CMP_IndicatorsRefreshMap();
		} else {
			g_localMenuFlags &= ~cmp::PoseFlag::Pipboy;
		}
	} else if (std::strcmp(menuName, "ContainerMenu") == 0 || std::strcmp(menuName, "WorkbenchMenu") == 0) {
		if (opening) {
			g_localMenuFlags |= cmp::PoseFlag::Menu;
		} else {
			g_localMenuFlags &= ~cmp::PoseFlag::Menu;
		}
	}
}

std::uint32_t CMP_LocalMenuFlags()
{
	return g_localMenuFlags;
}
