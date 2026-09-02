#include <cmath>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "cmp_blobs.hpp"
#include "cmp_json.hpp"
#include "cmp_net.hpp"
#include "cmp_protocol.hpp"
#include "cmp_util.hpp"
#include "config.hpp"
#include "sim.hpp"

namespace {

int g_fails = 0;
int g_checks = 0;

void check(bool cond, const char* expr, const char* file, int line)
{
	++g_checks;
	if (!cond) {
		++g_fails;
		std::cerr << "FAIL " << file << ":" << line << " " << expr << "\n";
	}
}

#define CHECK(cond) check(static_cast<bool>(cond), #cond, __FILE__, __LINE__)

bool nearly(float a, float b, float eps = 0.01f)
{
	return std::fabs(a - b) <= eps;
}

int test_protocol()
{
	const auto hello = cmp::make_hello(
		"verylongname-that-should-truncate-past-thirty-one",
		"verylong-player-key-that-should-also-truncate-past-31",
		true,
		cmp::kCommonwealthWorldspace,
		1.5f,
		11.0f,
		0x1A,
		cmp::kSanctuaryX,
		cmp::kSanctuaryY,
		cmp::kSanctuaryZ,
		false);
	CHECK(hello.header.size == sizeof(cmp::Hello));
	CHECK(hello.protocol == cmp::kProtocolVersion);
	CHECK(hello.pluginVersion == cmp::kPluginVersion);
	CHECK(hello.inWorld == 1);
	CHECK(hello.flags == 0);
	CHECK(sizeof(cmp::Hello) == 135);
	CHECK(sizeof(cmp::Welcome) == 30);
	CHECK(sizeof(cmp::SessionQuery) == 20);
	CHECK(sizeof(cmp::SessionInfo) == 142);

	const auto helloReq = cmp::make_hello(
		"g",
		"k",
		true,
		cmp::kCommonwealthWorldspace,
		0.0f,
		10.0f,
		0,
		1.0f,
		2.0f,
		3.0f,
		false,
		cmp::kHelloFlagRequireHost);
	CHECK(helloReq.flags == cmp::kHelloFlagRequireHost);

	const auto query = cmp::make_session_query();
	CHECK(query.header.type == static_cast<std::uint8_t>(cmp::Msg::SessionQuery));
	CHECK(query.pluginVersion == cmp::kPluginVersion);
	CHECK(cmp::header_ok(query.header, sizeof(query)));

	const auto info = cmp::make_session_info(3, 0x3C, 10.0f, 20.0f, 30.0f, 2, true, false, "TestServer", 8, "hello motd");
	CHECK(info.haveHost == 1);
	CHECK(info.hostPeerId == 3);
	CHECK(info.clientCount == 2);
	CHECK(info.hostInterior == 0);
	CHECK(info.maxPlayers == 8);
	CHECK(nearly(info.hostX, 10.0f));
	CHECK(std::string_view(info.serverName) == "TestServer");
	CHECK(std::string_view(info.motd) == "hello motd");
	CHECK(cmp::msg_name(cmp::Msg::SessionQuery) == "SessionQuery");
	CHECK(std::string_view(cmp::reject_name(cmp::RejectReason::NoHost)) == "no host");
	CHECK(std::string_view(cmp::reject_name(cmp::RejectReason::Full)) == "server full");
	CHECK(std::string_view(cmp::reject_name(cmp::RejectReason::HostNotStreaming)) == "host not streaming");
	CHECK(std::string_view(cmp::reject_name(cmp::RejectReason::PluginVersion)) == "plugin version");
	CHECK(std::string_view(cmp::reject_name(cmp::RejectReason::Banned)) == "banned");
	CHECK(std::string_view(cmp::reject_name(cmp::RejectReason::Password)) == "password");
	CHECK(std::string_view(cmp::reject_name(cmp::RejectReason::ModMismatch)) == "mod mismatch");
	CHECK(std::string_view(cmp::reject_name(cmp::RejectReason::None)) == "reject");
	CHECK(cmp::msg_name(cmp::Msg::Hello) == "Hello");
	CHECK(cmp::msg_name(cmp::Msg::Bye) == "Bye");
	CHECK(cmp::msg_name(cmp::Msg::AppearanceChunk) == "AppearanceChunk");
	CHECK(cmp::msg_name(static_cast<cmp::Msg>(255)) == "Unknown");

	const auto helloIn = cmp::make_hello("in", "k", true, 0x1A26F, 0, 12.0f, 0, 1, 2, 3, true);
	CHECK(helloIn.interior == 1);
	CHECK(helloIn.locationFormId == 0x1A26F);

	const auto bye = cmp::make_bye(9);
	CHECK(sizeof(cmp::Bye) == 16);
	CHECK(bye.peerId == 9);
	CHECK(bye.header.size == sizeof(cmp::Bye));
	CHECK(bye.header.type == static_cast<std::uint8_t>(cmp::Msg::Bye));

	const auto longReject = cmp::make_reject(
		cmp::RejectReason::HostNotStreaming,
		"xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxMORE");
	CHECK(std::strlen(longReject.message) == 95);
	CHECK(sizeof(cmp::Reject) == 112);

	const auto longInfo = cmp::make_session_info(
		1, 0x3C, 0, 0, 0, 0, false, true,
		"0123456789012345678901234567890123456789",
		4,
		"0123456789012345678901234567890123456789012345678901234567890123456789");
	CHECK(longInfo.hostInterior == 1);
	CHECK(std::strlen(longInfo.serverName) == 31);
	CHECK(std::strlen(longInfo.motd) == 63);

	cmp::Header exact = hello.header;
	exact.size = static_cast<std::uint16_t>(sizeof(cmp::Header));
	CHECK(!cmp::header_ok(exact, sizeof(cmp::Header)));
	CHECK(cmp::header_ok(hello.header, hello.header.size));
	CHECK(!cmp::header_ok(hello.header, hello.header.size - 1));
	CHECK(std::strlen(hello.name) <= 31);
	CHECK(std::strlen(hello.playerKey) <= 31);
	CHECK(std::string_view(hello.name) == "verylongname-that-should-trunca");
	CHECK(std::string_view(hello.playerKey) == "verylong-player-key-that-should");
	CHECK(cmp::header_ok(hello.header, sizeof(hello)));

	CHECK(sizeof(cmp::PlayerPose) == 56);
	CHECK(sizeof(cmp::ActorPose) == 64);
	CHECK(sizeof(cmp::Hit) == 28);
	CHECK(sizeof(cmp::Chat) == 96);
	CHECK(sizeof(cmp::Kick) == 96);
	CHECK(sizeof(cmp::Heartbeat) == 20);
	CHECK(sizeof(cmp::Teleport) == 32);
	CHECK(cmp::msg_name(cmp::Msg::Teleport) == "Teleport");
	CHECK(cmp::kProtocolVersion == 12);
	CHECK(cmp::kPluginVersion == 12);
	CHECK(sizeof(cmp::UdpBind) == 20);
	CHECK(cmp::msg_name(cmp::Msg::UdpBind) == "UdpBind");
	CHECK(cmp::msg_is_tcp(cmp::Msg::Chat));
	CHECK(cmp::msg_is_tcp(cmp::Msg::Hit));
	CHECK(cmp::msg_is_udp(cmp::Msg::PlayerPose));
	CHECK(cmp::msg_is_udp(cmp::Msg::UdpBind));
	CHECK(!cmp::msg_is_tcp(cmp::Msg::PlayerPose));
	CHECK(cmp::clamp_hit_damage(-3.0f) == 0.0f);
	CHECK(cmp::clamp_hit_damage(0.0f) == 0.0f);
	CHECK(cmp::clamp_hit_damage(40.0f) == 40.0f);
	CHECK(cmp::clamp_hit_damage(9999.0f) == 500.0f);
	CHECK(cmp::msg_name(cmp::Msg::Hit) == "Hit");
	CHECK(cmp::msg_name(cmp::Msg::Chat) == "Chat");
	CHECK(cmp::msg_name(cmp::Msg::Kick) == "Kick");
	const auto infoFlags = cmp::make_session_info(1, 0x3C, 0, 0, 0, 1, true, false, "Srv", 8, "motd", cmp::kSessionPvpEnabled);
	CHECK(infoFlags.sessionFlags == cmp::kSessionPvpEnabled);
	const auto welcomeFlags = cmp::make_welcome(3, 0, true, true, cmp::kSessionPvpEnabled | cmp::kSessionPasswordRequired);
	CHECK(welcomeFlags.sessionFlags == (cmp::kSessionPvpEnabled | cmp::kSessionPasswordRequired));
	const auto helloPwd = cmp::make_hello("n", "k", true, 0x3C, 0, 10, 0, 1, 2, 3, false, 0, 0xABCD1234u, "secret");
	CHECK(helloPwd.modHash == 0xABCD1234u);
	CHECK(std::string_view(helloPwd.password) == "secret");
	const auto hit = cmp::make_hit(1, 7, 40.5f);
	CHECK(hit.header.type == static_cast<std::uint8_t>(cmp::Msg::Hit));
	CHECK(hit.attackerPeerId == 1);
	CHECK(hit.targetPeerId == 7);
	CHECK(hit.damage == 40.5f);
	CHECK(cmp::header_ok(hit.header, sizeof(hit)));
	CHECK(cmp::has_pose_flag(cmp::PoseFlag::Dead, cmp::PoseFlag::Dead));
	CHECK(cmp::has_pose_flag(cmp::pose_anim_flags(0), cmp::PoseFlag::Drawn));
	CHECK(cmp::has_pose_flag(cmp::pose_anim_flags(1), cmp::PoseFlag::Sighted));
	CHECK(cmp::has_pose_flag(cmp::pose_anim_flags(2), cmp::PoseFlag::Attacking));
	CHECK(cmp::has_pose_flag(cmp::pose_anim_flags(3), cmp::PoseFlag::Reloading));
	CHECK(cmp::has_pose_flag(cmp::pose_anim_flags(4), cmp::PoseFlag::Jumping));
	CHECK(cmp::has_pose_flag(cmp::pose_anim_flags(5), cmp::PoseFlag::Sneak));
	CHECK(cmp::has_pose_flag(cmp::pose_anim_flags(6), cmp::PoseFlag::Sprint));
	CHECK(cmp::pose_anim_flags(7) == 0);
	CHECK(cmp::pose_anim_holds_still(cmp::pose_anim_flags(0)));
	CHECK(cmp::pose_anim_holds_still(cmp::pose_anim_flags(4)));
	CHECK(!cmp::pose_anim_holds_still(cmp::pose_anim_flags(5)));
	CHECK(!cmp::pose_anim_holds_still(0));
	CHECK(std::string_view(cmp::pose_anim_name(cmp::PoseFlag::Drawn | cmp::PoseFlag::Attacking)) == "fire");
	CHECK(std::string_view(cmp::pose_anim_name(0)) == "walk");
	CHECK(cmp::msg_name(cmp::Msg::ActorPose) == "ActorPose");
	const auto actorPose = cmp::make_actor_pose(0x1A4D7, 0x1A4D8, cmp::kCommonwealthWorldspace, 1, 2, 3, 0.5f, 0.1f, 40, 5, 6, cmp::PoseFlag::Drawn, true, false);
	CHECK(actorPose.header.type == static_cast<std::uint8_t>(cmp::Msg::ActorPose));
	CHECK(actorPose.refFormId == 0x1A4D7);
	CHECK(actorPose.baseFormId == 0x1A4D8);
	CHECK(actorPose.unique == 1);
	CHECK(actorPose.dead == 0);
	CHECK(cmp::has_pose_flag(actorPose.flags, cmp::PoseFlag::Drawn));
	CHECK(cmp::header_ok(actorPose.header, sizeof(actorPose)));
	const auto pose = cmp::make_pose(
		3,
		cmp::kCommonwealthWorldspace,
		1.0f,
		2.0f,
		3.0f,
		0.5f,
		0.1f,
		80.0f,
		10.0f,
		20.0f,
		cmp::PoseFlag::Drawn | cmp::PoseFlag::Sprint);
	CHECK(pose.header.size == sizeof(cmp::PlayerPose));
	CHECK(pose.peerId == 3);
	CHECK(nearly(pose.pitch, 0.1f));
	CHECK(nearly(pose.speed, 80.0f));
	CHECK(nearly(pose.vx, 10.0f));
	CHECK(nearly(pose.vy, 20.0f));
	CHECK(cmp::has_pose_flag(pose.flags, cmp::PoseFlag::Drawn));
	CHECK(cmp::has_pose_flag(pose.flags, cmp::PoseFlag::Sprint));
	CHECK(!cmp::has_pose_flag(pose.flags, cmp::PoseFlag::Sneak));
	CHECK(cmp::header_ok(pose.header, sizeof(pose)));
	const auto allFlags = cmp::make_pose(
		1, cmp::kCommonwealthWorldspace, 0, 0, 0, 0, 0, 0, 0, 0,
		cmp::PoseFlag::Sneak | cmp::PoseFlag::Sprint | cmp::PoseFlag::Drawn | cmp::PoseFlag::Reloading
			| cmp::PoseFlag::Attacking | cmp::PoseFlag::Jumping | cmp::PoseFlag::Sighted);
	CHECK(cmp::has_pose_flag(allFlags.flags, cmp::PoseFlag::Sneak));
	CHECK(cmp::has_pose_flag(allFlags.flags, cmp::PoseFlag::Reloading));
	CHECK(cmp::has_pose_flag(allFlags.flags, cmp::PoseFlag::Attacking));
	CHECK(cmp::has_pose_flag(allFlags.flags, cmp::PoseFlag::Jumping));
	CHECK(cmp::has_pose_flag(allFlags.flags, cmp::PoseFlag::Sighted));

	cmp::Header bad = hello.header;
	bad.magic[0] = 'X';
	CHECK(!cmp::header_ok(bad, sizeof(hello)));
	bad = hello.header;
	bad.size = 4;
	CHECK(!cmp::header_ok(bad, sizeof(hello)));
	bad = hello.header;
	bad.size = 200;
	CHECK(!cmp::header_ok(bad, sizeof(hello)));

	const auto welcome = cmp::make_welcome(7, 0, true, true);
	CHECK(welcome.peerId == 7);
	CHECK(welcome.fakePeerId == 0);
	CHECK(welcome.isNewPlayer == 1);
	CHECK(welcome.isHost == 1);

	const auto snap = cmp::make_world_snapshot(
		10.0f, 2.0f, 3, 0x3C, 1.0f, 2.0f, 3.0f, 1,
		0x3C, cmp::kSanctuaryX, cmp::kSanctuaryY, cmp::kSanctuaryZ, true);
	CHECK(nearly(snap.hostX, 1.0f));
	CHECK(nearly(snap.hostY, 2.0f));
	CHECK(nearly(snap.hostZ, 3.0f));
	CHECK(snap.hostPeerId == 1);
	CHECK(nearly(snap.placeX, cmp::kSanctuaryX));
	CHECK(nearly(snap.placeY, cmp::kSanctuaryY));
	CHECK(nearly(snap.placeZ, cmp::kSanctuaryZ));
	CHECK(snap.isNewPlayer == 1);
	CHECK(nearly(snap.gameHour, 10.0f));
	CHECK(nearly(snap.gameDaysPassed, 2.0f));
	CHECK(snap.weatherFormId == 3);
	CHECK(snap.hostLocationFormId == 0x3C);
	CHECK(snap.placeLocationFormId == 0x3C);

	const auto reject = cmp::make_reject(cmp::RejectReason::NotInWorld, "load a save and enter the world");
	CHECK(reject.reason == static_cast<std::uint32_t>(cmp::RejectReason::NotInWorld));
	CHECK(std::string_view(reject.message) == "load a save and enter the world");

	CHECK(cmp::forbidden_actor_base(0x7));
	CHECK(cmp::forbidden_actor_base(0x20593));
	CHECK(cmp::forbidden_actor_base(0x01000007));
	CHECK(cmp::forbidden_actor_base(0x05020593));
	CHECK(!cmp::forbidden_actor_base(0x13746));
	return 0;
}

int test_keys()
{
	CHECK(cmp::sanitize_player_key("ok_Key-1") == "ok_Key-1");
	CHECK(cmp::sanitize_player_key("bad key!@#") == "badkey");
	CHECK(cmp::sanitize_player_key("!!!", "player") == "player");
	CHECK(cmp::sanitize_player_key("!!!", {}) == "");
	CHECK(cmp::sanitize_player_key("", "player") == "player");
	CHECK(cmp::sanitize_player_key("a-b_c") == "a-b_c");
	CHECK(cmp::sanitize_player_key("---") == "---");
	CHECK(cmp::sanitize_player_key("   ", "player") == "player");
	CHECK(cmp::sanitize_player_key("!!!@@@", "fallback") == "fallback");
	const auto longKey = cmp::sanitize_player_key("abcdefghijklmnopqrstuvwxyz0123456789");
	CHECK(longKey.size() == 31);
	CHECK(longKey == "abcdefghijklmnopqrstuvwxyz01234");
	return 0;
}

int test_forms()
{
	const auto vanilla = cmp::pack_form_id(0x00013746, "Fallout4.esm", false);
	CHECK(vanilla.raw == 0x00013746);
	CHECK(std::string_view(vanilla.plugin) == "Fallout4.esm");

	const auto full = cmp::pack_form_id(0x05001234, "Some.esp", false);
	CHECK(full.raw == 0x00001234);

	const auto light = cmp::pack_form_id(0x00000ABC, "Light.esl", true);
	CHECK(light.raw == 0x00000ABC);

	CHECK(cmp::full_form_id(0x00001234, 0x05, false) == 0x05001234);
	CHECK(cmp::full_form_id(0x00000ABC, 0x01, true) == 0x00000ABC);
	CHECK(cmp::full_form_id(0, 5, false) == 0);
	CHECK(cmp::full_form_id(0x00001234, 0, false) == 0x00001234);

	const auto lightKeep = cmp::pack_form_id(0x05001234, "Light.esl", true);
	CHECK(lightKeep.raw == 0x05001234);
	const auto longPlugin = cmp::pack_form_id(1, "012345678901234567890123456789", false);
	CHECK(std::strlen(longPlugin.plugin) == 23);
	return 0;
}

cmp::InventorySheet sample_sheet(bool withForbidden)
{
	cmp::InventorySheet s;
	cmp::copy_cstr(s.name, sizeof(s.name), "Nora");
	s.sex = 1;
	s.race = cmp::pack_form_id(0x00013746, "Fallout4.esm", false);
	s.worn.push_back(cmp::pack_form_id(0x000A183B, "Fallout4.esm", false));
	s.worn.push_back(cmp::pack_form_id(0x0004A9C8, "Fallout4.esm", false));
	if (withForbidden) {
		s.worn.push_back(cmp::pack_form_id(0x7, "Fallout4.esm", false));
	}
	s.stacks.push_back({ cmp::pack_form_id(0x00033102, "Fallout4.esm", false), 12 });
	s.stacks.push_back({ cmp::pack_form_id(0x0001F66B, "Fallout4.esm", false), 1 });
	s.stacks.push_back({ cmp::pack_form_id(0x0000E376, "Fallout4.esm", false), 40 });
	if (withForbidden) {
		s.stacks.push_back({ cmp::pack_form_id(0x7, "Fallout4.esm", false), 1 });
	}
	return s;
}

int test_inventory_blob()
{
	std::vector<std::uint8_t> bytes;
	CHECK(cmp::encode_inventory_sheet(sample_sheet(false), bytes));
	cmp::InventorySheet back;
	CHECK(cmp::decode_inventory_sheet(bytes, back));
	CHECK(std::string_view(back.name) == "Nora");
	CHECK(back.sex == 1);
	CHECK(back.race.raw == 0x00013746);
	CHECK(back.worn.size() == 2);
	CHECK(back.stacks.size() == 3);
	CHECK(back.stacks[0].count == 12);
	CHECK(back.stacks[2].count == 40);

	std::vector<std::uint8_t> dirty;
	CHECK(cmp::encode_inventory_sheet(sample_sheet(true), dirty));
	cmp::InventorySheet filtered;
	CHECK(cmp::decode_inventory_sheet(dirty, filtered));
	CHECK(filtered.worn.size() == 2);
	CHECK(filtered.stacks.size() == 3);
	for (const auto& w : filtered.worn) {
		CHECK(!cmp::forbidden_actor_base(w.raw));
	}
	for (const auto& st : filtered.stacks) {
		CHECK(!cmp::forbidden_actor_base(st.form.raw));
	}

	cmp::InventorySheet empty;
	cmp::copy_cstr(empty.name, sizeof(empty.name), "Empty");
	empty.sex = 0;
	empty.race = cmp::pack_form_id(0x00013746, "Fallout4.esm", false);
	std::vector<std::uint8_t> emptyBytes;
	CHECK(cmp::encode_inventory_sheet(empty, emptyBytes));
	cmp::InventorySheet emptyBack;
	CHECK(cmp::decode_inventory_sheet(emptyBytes, emptyBack));
	CHECK(emptyBack.worn.empty());
	CHECK(emptyBack.stacks.empty());
	CHECK(emptyBack.sex == 0);

	cmp::InventorySheet over;
	over.race = cmp::pack_form_id(0x00013746, "Fallout4.esm", false);
	for (int i = 0; i < cmp::kMaxWorn + 8; ++i) {
		over.worn.push_back(cmp::pack_form_id(0x1000 + static_cast<std::uint32_t>(i), "Fallout4.esm", false));
	}
	for (int i = 0; i < cmp::kMaxStacks + 8; ++i) {
		over.stacks.push_back({ cmp::pack_form_id(0x2000 + static_cast<std::uint32_t>(i), "Fallout4.esm", false), 1 });
	}
	std::vector<std::uint8_t> overBytes;
	CHECK(cmp::encode_inventory_sheet(over, overBytes));
	cmp::InventorySheet overBack;
	CHECK(cmp::decode_inventory_sheet(overBytes, overBack));
	CHECK(overBack.worn.size() == static_cast<std::size_t>(cmp::kMaxWorn));
	CHECK(overBack.stacks.size() == static_cast<std::size_t>(cmp::kMaxStacks));

	cmp::InventorySheet zeros;
	zeros.race = cmp::pack_form_id(0x00013746, "Fallout4.esm", false);
	zeros.worn.push_back(cmp::pack_form_id(0, "Fallout4.esm", false));
	zeros.worn.push_back(cmp::pack_form_id(0x000A183B, "Fallout4.esm", false));
	zeros.stacks.push_back({ cmp::pack_form_id(0, "Fallout4.esm", false), 3 });
	zeros.stacks.push_back({ cmp::pack_form_id(0x00033102, "Fallout4.esm", false), 2 });
	std::vector<std::uint8_t> zeroBytes;
	CHECK(cmp::encode_inventory_sheet(zeros, zeroBytes));
	cmp::InventorySheet zeroBack;
	CHECK(cmp::decode_inventory_sheet(zeroBytes, zeroBack));
	CHECK(zeroBack.worn.size() == 1);
	CHECK(zeroBack.worn[0].raw == 0x000A183B);
	CHECK(zeroBack.stacks.size() == 1);
	CHECK(zeroBack.stacks[0].count == 2);

	bytes[0] = 'X';
	cmp::InventorySheet bad;
	CHECK(!cmp::decode_inventory_sheet(bytes, bad));
	return 0;
}

int test_chunking()
{
	auto run = [](std::size_t n, std::size_t expectPackets) {
		std::vector<std::uint8_t> blob(n);
		for (std::size_t i = 0; i < n; ++i) {
			blob[i] = static_cast<std::uint8_t>(i * 3 + 1);
		}
		std::vector<std::vector<std::uint8_t>> packets;
		CHECK(cmp::split_blob_chunks(cmp::Msg::InventoryChunk, 9, blob, packets));
		CHECK(packets.size() == expectPackets);

		cmp::BlobAssembly a;
		std::vector<std::uint8_t> got;
		if (packets.size() == 2) {
			const auto second = cmp::assemble_blob_chunk(a, packets[1], got);
			CHECK(second == cmp::AssembleStatus::Pending);
			const auto first = cmp::assemble_blob_chunk(a, packets[0], got);
			CHECK(first == cmp::AssembleStatus::Complete);
		} else {
			CHECK(cmp::assemble_blob_chunk(a, packets[0], got) == cmp::AssembleStatus::Complete);
		}
		CHECK(got == blob);
	};

	run(1, 1);
	run(cmp::kBlobPayloadMax, 1);
	run(cmp::kBlobPayloadMax + 1, 2);

	{
		std::vector<std::uint8_t> blob(cmp::kBlobPayloadMax * 2 + 3);
		for (std::size_t i = 0; i < blob.size(); ++i) {
			blob[i] = static_cast<std::uint8_t>(i * 5 + 2);
		}
		std::vector<std::vector<std::uint8_t>> packets;
		CHECK(cmp::split_blob_chunks(cmp::Msg::InventoryChunk, 4, blob, packets));
		CHECK(packets.size() == 3);
		cmp::BlobAssembly a;
		std::vector<std::uint8_t> got;
		CHECK(cmp::assemble_blob_chunk(a, packets[2], got) == cmp::AssembleStatus::Pending);
		CHECK(cmp::assemble_blob_chunk(a, packets[0], got) == cmp::AssembleStatus::Pending);
		CHECK(cmp::assemble_blob_chunk(a, packets[1], got) == cmp::AssembleStatus::Complete);
		CHECK(got == blob);
	}

	{
		std::vector<std::uint8_t> blob(8, 0xCD);
		std::vector<std::vector<std::uint8_t>> packets;
		CHECK(cmp::split_blob_chunks(cmp::Msg::AppearanceChunk, 1, blob, packets));
		auto mismatch = packets[0];
		cmp::BlobChunk chunk{};
		std::memcpy(&chunk, mismatch.data(), sizeof(chunk));
		chunk.blobBytes = 99;
		std::memcpy(mismatch.data(), &chunk, sizeof(chunk));
		cmp::BlobAssembly a;
		std::vector<std::uint8_t> got;
		CHECK(cmp::assemble_blob_chunk(a, mismatch, got) == cmp::AssembleStatus::Reject);
	}

	{
		std::vector<std::uint8_t> two(cmp::kBlobPayloadMax + 4, 1);
		std::vector<std::vector<std::uint8_t>> packets;
		CHECK(cmp::split_blob_chunks(cmp::Msg::InventoryChunk, 1, two, packets));
		CHECK(packets.size() == 2);
		cmp::BlobAssembly a;
		std::vector<std::uint8_t> got;
		CHECK(cmp::assemble_blob_chunk(a, packets[0], got) == cmp::AssembleStatus::Pending);
		std::vector<std::uint8_t> one(16, 2);
		std::vector<std::vector<std::uint8_t>> onePkt;
		CHECK(cmp::split_blob_chunks(cmp::Msg::InventoryChunk, 1, one, onePkt));
		CHECK(cmp::assemble_blob_chunk(a, onePkt[0], got) == cmp::AssembleStatus::Complete);
		CHECK(got == one);
	}

	std::vector<std::uint8_t> blob(8, 0xAB);
	std::vector<std::vector<std::uint8_t>> packets;
	CHECK(cmp::split_blob_chunks(cmp::Msg::AppearanceChunk, 1, blob, packets));
	CHECK(!packets.empty());
	auto truncated = packets[0];
	truncated.resize(sizeof(cmp::BlobChunk));
	cmp::BlobAssembly a;
	std::vector<std::uint8_t> got;
	CHECK(cmp::assemble_blob_chunk(a, truncated, got) == cmp::AssembleStatus::Reject);

	auto badIndex = packets[0];
	cmp::BlobChunk chunk{};
	std::memcpy(&chunk, badIndex.data(), sizeof(chunk));
	chunk.chunkIndex = 9;
	chunk.chunkCount = 1;
	std::memcpy(badIndex.data(), &chunk, sizeof(chunk));
	CHECK(cmp::assemble_blob_chunk(a, badIndex, got) == cmp::AssembleStatus::Reject);

	std::vector<std::vector<std::uint8_t>> empty;
	CHECK(!cmp::split_blob_chunks(cmp::Msg::InventoryChunk, 1, std::span<const std::uint8_t>(), empty));

	std::vector<std::uint8_t> tooBig(0x10000, 1);
	CHECK(!cmp::split_blob_chunks(cmp::Msg::InventoryChunk, 1, tooBig, empty));
	return 0;
}

int test_hostile_blobs()
{
	cmp::BlobAssembly a;
	std::vector<std::uint8_t> got;
	std::vector<std::uint8_t> huge(sizeof(cmp::BlobChunk) + 1, 0);
	cmp::BlobChunk chunk{};
	cmp::fill_header(chunk, cmp::Msg::InventoryChunk);
	chunk.header.size = static_cast<std::uint16_t>(sizeof(chunk) + 1);
	chunk.peerId = 1;
	chunk.chunkIndex = 0;
	chunk.chunkCount = 65535;
	chunk.blobBytes = 1;
	chunk.payloadBytes = 1;
	std::memcpy(huge.data(), &chunk, sizeof(chunk));
	CHECK(cmp::assemble_blob_chunk(a, huge, got) == cmp::AssembleStatus::Reject);
	CHECK(a.chunks.empty());

	chunk.chunkCount = 1;
	chunk.payloadBytes = static_cast<std::uint16_t>(cmp::kBlobPayloadMax + 1);
	std::vector<std::uint8_t> fat(sizeof(cmp::BlobChunk) + chunk.payloadBytes, 0);
	std::memcpy(fat.data(), &chunk, sizeof(chunk));
	CHECK(cmp::assemble_blob_chunk(a, fat, got) == cmp::AssembleStatus::Reject);

	CHECK(cmp::assemble_blob_chunk(a, std::span<const std::uint8_t>(), got) == cmp::AssembleStatus::Reject);

	std::vector<std::uint8_t> good;
	CHECK(cmp::encode_inventory_sheet(sample_sheet(false), good));
	auto shortInv = good;
	shortInv.resize(10);
	cmp::InventorySheet sheet;
	CHECK(!cmp::decode_inventory_sheet(shortInv, sheet));
	CHECK(!cmp::decode_inventory_sheet(std::span<const std::uint8_t>(), sheet));

	cmp::BlobWriter w;
	w.u32(cmp::kInvMagic);
	w.u16(cmp::kInvVersion);
	char name[32]{};
	std::memset(name, 'A', 32);
	w.raw(name, 32);
	w.u8(0);
	w.write_form(cmp::pack_form_id(0x13746, "Fallout4.esm", false));
	w.u8(200);
	cmp::InventorySheet overWorn;
	CHECK(!cmp::decode_inventory_sheet(w.bytes, overWorn));

	cmp::BlobWriter w2;
	w2.u32(cmp::kInvMagic);
	w2.u16(cmp::kInvVersion);
	char name2[32]{};
	w2.raw(name2, 32);
	w2.u8(0);
	w2.write_form({});
	w2.u8(0);
	w2.u16(1000);
	cmp::InventorySheet overStacks;
	CHECK(!cmp::decode_inventory_sheet(w2.bytes, overStacks));

	cmp::BlobWriter w3;
	w3.u32(cmp::kInvMagic);
	w3.u16(cmp::kInvVersion);
	char unterminated[32];
	std::memset(unterminated, 'B', 32);
	w3.raw(unterminated, 32);
	w3.u8(0);
	w3.write_form({});
	w3.u8(0);
	w3.u16(0);
	cmp::InventorySheet named;
	CHECK(cmp::decode_inventory_sheet(w3.bytes, named));
	CHECK(std::strlen(named.name) == 31);

	cmp::BlobReader emptyReader{ std::span<const std::uint8_t>() };
	std::uint32_t v = 99;
	CHECK(!emptyReader.u32(v));
	CHECK(v == 99);
	std::uint16_t s = 7;
	CHECK(!emptyReader.u16(s));
	CHECK(s == 7);
	float f = 3.0f;
	CHECK(!emptyReader.f32(f));
	CHECK(f == 3.0f);
	return 0;
}

int test_copy_and_json()
{
	char none[1] = { 'X' };
	cmp::copy_cstr(none, 0, "hi");
	CHECK(none[0] == 'X');
	char one[1] = { 'X' };
	cmp::copy_cstr(one, 1, "hi");
	CHECK(one[0] == '\0');
	cmp::copy_cstr(nullptr, 8, "hi");

	const auto json = std::string("{\n  \"key\": \"persist-key\",\n  \"x\": 9.5,\n  \"havePose\": 1\n}\n");
	CHECK(cmp::json_quoted(json, "key") == "persist-key");
	CHECK(cmp::json_quoted(json, "missing").empty());
	CHECK(cmp::json_quoted("{\"key\":", "key").empty());
	CHECK(cmp::json_quoted("key: nope", "key").empty());
	CHECK(cmp::json_quoted("", "key").empty());
	CHECK(nearly(static_cast<float>(cmp::json_number(json, "x", -1)), 9.5f));
	CHECK(cmp::json_number(json, "havePose", 0) == 1);
	CHECK(cmp::json_number(json, "nope", 42) == 42);
	CHECK(cmp::json_number("{\"x\": }", "x", 7) == 7);
	CHECK(cmp::json_number("{\"x\":", "x", 7) == 7);
	CHECK(cmp::json_number("{\"x\": abc}", "x", 7) == 7);
	CHECK(cmp::json_number(json, nullptr, 3) == 3);
	CHECK(cmp::json_quoted(json, nullptr).empty());
	CHECK(cmp::json_quoted("{\"key\" \"value\"}", "key").empty());
	CHECK(cmp::json_quoted("{\"key\": \"noend}", "key").empty());
	CHECK(nearly(static_cast<float>(cmp::json_number("{\"x\": -3.5}", "x", 0)), -3.5f));
	CHECK(cmp::json_number("{\"n\": 42}", "n", 0) == 42);
	CHECK(cmp::json_number("{\"x\": \t\n  4}", "x", 0) == 4);
	return 0;
}

int test_config()
{
	CHECK(trim_ini("  hi\t") == "hi");
	CHECK(trim_ini("\rname") == "name");
	CHECK(trim_ini("") == "");
	CHECK(parse_bool_ini("1", false));
	CHECK(parse_bool_ini("true", false));
	CHECK(parse_bool_ini("True", false));
	CHECK(parse_bool_ini("TRUE", false));
	CHECK(parse_bool_ini("yes", false));
	CHECK(parse_bool_ini("on", false));
	CHECK(!parse_bool_ini("0", true));
	CHECK(!parse_bool_ini("false", true));
	CHECK(!parse_bool_ini("False", true));
	CHECK(!parse_bool_ini("FALSE", true));
	CHECK(!parse_bool_ini("no", true));
	CHECK(!parse_bool_ini("off", true));
	CHECK(parse_bool_ini("", true));
	CHECK(!parse_bool_ini("", false));
	CHECK(parse_bool_ini("maybe", true));
	CHECK(!parse_bool_ini("maybe", false));

	namespace fs = std::filesystem;
	const auto dir = fs::temp_directory_path() / "cmp-units-ini";
	fs::create_directories(dir);
	const auto path = dir / "server.ini";
	fs::remove(path);
	bool created = false;
	ServerConfig cfg;
	CHECK(ensure_server_ini(path.string(), cfg, &created));
	CHECK(created);
	CHECK(cfg.name == "CommonwealthMP");
	CHECK(cfg.maxPlayers == 8);
	CHECK(cfg.port == cmp::kDefaultPort);
	CHECK(nearly(cfg.interestUu, 20000.0f, 0.1f));

	{
		std::ofstream out(path, std::ios::trunc);
		out << "# comment\n"
			<< "; also\n"
			<< "name = CustomSrv\n"
			<< "motd=hi\n"
			<< "max_players=0\n"
			<< "interest_uu=-5\n"
			<< "port=notanumber\n"
			<< "verbose=yes\n"
			<< "pvp=off\n"
			<< "password=secret\n"
			<< "mod_hash=0xABCD1234\n"
			<< "noportvalue\n"
			<< "\n";
	}
	ServerConfig loaded;
	CHECK(load_server_ini(path.string(), loaded));
	CHECK(loaded.name == "CustomSrv");
	CHECK(loaded.motd == "hi");
	CHECK(loaded.maxPlayers == 1);
	CHECK(loaded.interestUu == 0.0f);
	CHECK(loaded.verbose);
	CHECK(!loaded.pvp);
	CHECK(loaded.password == "secret");
	CHECK(loaded.modHash == 0xABCD1234u);
	CHECK(loaded.port == cmp::kDefaultPort);

	created = false;
	ServerConfig again;
	CHECK(ensure_server_ini(path.string(), again, &created));
	CHECK(!created);
	CHECK(again.name == "CustomSrv");
	return 0;
}

int test_sim()
{
	const auto here = cmp::make_pose(1, cmp::kCommonwealthWorldspace, 0, 0, 0, 0);
	const auto nearPose = cmp::make_pose(2, cmp::kCommonwealthWorldspace, 30, 0, 0, 0);
	const auto farPose = cmp::make_pose(3, cmp::kCommonwealthWorldspace, 1000, 0, 0, 0);
	const auto otherCell = cmp::make_pose(4, 0x1A26F, 1000, 0, 0, 0);

	CHECK(cmp::in_interest(here, true, farPose, true, 0.0f));
	CHECK(cmp::in_interest(here, true, farPose, true, -1.0f));
	CHECK(cmp::in_interest(here, false, farPose, true, 50.0f));
	CHECK(cmp::in_interest(here, true, farPose, false, 50.0f));
	CHECK(cmp::in_interest(here, true, otherCell, true, 50.0f));
	CHECK(cmp::in_interest(here, true, nearPose, true, 50.0f));
	CHECK(!cmp::in_interest(here, true, farPose, true, 50.0f));

	const auto actorNear = cmp::make_actor_pose(0x10, 0x11, cmp::kCommonwealthWorldspace, 20, 0, 0, 0);
	const auto actorFar = cmp::make_actor_pose(0x10, 0x11, cmp::kCommonwealthWorldspace, 1000, 0, 0, 0);
	CHECK(cmp::in_interest(actorNear, here, true, 50.0f));
	CHECK(!cmp::in_interest(actorFar, here, true, 50.0f));
	CHECK(cmp::in_interest(actorFar, otherCell, true, 50.0f));

	std::unordered_map<std::string, cmp::RateBucket> buckets;
	CHECK(cmp::allow_rate(buckets, "a", 1.0, 2));
	CHECK(cmp::allow_rate(buckets, "a", 1.1, 2));
	CHECK(!cmp::allow_rate(buckets, "a", 1.2, 2));
	CHECK(cmp::allow_rate(buckets, "a", 2.1, 2));
	CHECK(cmp::allow_rate(buckets, "b", 1.0, 1));
	return 0;
}

int test_tcp_send_queue()
{
	cmp::TcpSendQueue q;
	CHECK(q.empty());
	const char a[] = "hello";
	const char b[] = "world!!";
	CHECK(q.append(a, 5));
	CHECK(q.append(b, 7));
	CHECK(q.pending() == 12);
	CHECK(!q.empty());
	q.clear();
	CHECK(q.empty());
	std::vector<std::uint8_t> big(cmp::kTcpSendQueueMax, 0xAB);
	CHECK(q.append(big.data(), static_cast<int>(big.size())));
	CHECK(q.pending() == cmp::kTcpSendQueueMax);
	CHECK(!q.append(a, 1));
	q.clear();
	CHECK(q.append(a, 5));
	CHECK(!q.append(big.data(), static_cast<int>(big.size())));
	return 0;
}

}  // namespace

int main()
{
	test_protocol();
	test_keys();
	test_forms();
	test_inventory_blob();
	test_chunking();
	test_hostile_blobs();
	test_copy_and_json();
	test_config();
	test_sim();
	test_tcp_send_queue();
	if (g_fails) {
		std::cerr << "cmp_units: " << g_fails << " failed / " << g_checks << " checks\n";
		return 1;
	}
	std::cout << "cmp_units: " << g_checks << " checks ok\n";
	return 0;
}
