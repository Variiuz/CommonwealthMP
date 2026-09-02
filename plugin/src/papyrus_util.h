#pragma once

template <class... Args>
void CMP_CallActorPapyrus(RE::Actor* actor, const char* fn, Args&&... args)
{
	auto* game = RE::GameVM::GetSingleton();
	if (!game || !actor || !fn || !fn[0]) {
		return;
	}
	auto vm = game->GetVM();
	if (!vm) {
		return;
	}
	auto& handles = vm->GetObjectHandlePolicy();
	const auto handle = handles.GetHandleForObject(RE::BSScript::GetVMTypeID<RE::Actor>(), actor);
	if (handle == handles.EmptyHandle()) {
		return;
	}
	RE::BSTSmartPointer<RE::BSScript::IStackCallbackFunctor> cb{};
	vm->DispatchMethodCall(
		static_cast<std::uint64_t>(handle),
		RE::BSFixedString("Actor"),
		RE::BSFixedString(fn),
		cb,
		std::forward<Args>(args)...);
}
