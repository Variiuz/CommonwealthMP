#include "pch.h"
#include "net/internal.h"

namespace cmp_net {

LocalWorld ReadLocalWorld()
{
	LocalWorld w;
	auto* player = RE::PlayerCharacter::GetSingleton();
	if (!player) {
		return w;
	}
	if (auto* cell = player->GetParentCell()) {
		w.inWorld = true;
		w.interior = cell->IsInterior();
		if (w.interior) {
			w.location = cell->GetFormID();
		} else if (cell->worldSpace) {
			w.location = cell->worldSpace->GetFormID();
		} else {
			w.location = cell->GetFormID();
		}
	}
	const auto pos = player->GetPosition();
	w.x = pos.x;
	w.y = pos.y;
	w.z = pos.z;
	w.yaw = player->GetHeading();
	if (auto* cal = RE::Calendar::GetSingleton()) {
		if (cal->gameDaysPassed) {
			w.days = cal->gameDaysPassed->GetValue();
		}
		if (cal->gameHour) {
			w.hour = cal->gameHour->GetValue();
		}
	}
	if (auto* sky = RE::Sky::GetSingleton()) {
		if (auto* weather = sky->currentWeather ? sky->currentWeather : sky->overrideWeather) {
			w.weather = weather->GetFormID() & 0x00FFFFFF;
		}
	}
	return w;
}

}  // namespace cmp_net
