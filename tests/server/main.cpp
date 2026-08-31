#include <WinSock2.h>
#include <WS2tcpip.h>

#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <span>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#include "cmp_blobs.hpp"
#include "cmp_protocol.hpp"

#pragma comment(lib, "ws2_32.lib")

namespace fs = std::filesystem;

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

bool nearly(float a, float b, float eps = 1.0f)
{
	return std::fabs(a - b) <= eps;
}

struct Options {
	std::string host{ "127.0.0.1" };
	std::uint16_t port{ 17778 };
	std::string sessionDir;
	std::string which;
};

struct UdpClient {
	SOCKET sock{ INVALID_SOCKET };
	sockaddr_in dest{};

	bool open(const char* host, std::uint16_t port)
	{
		sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
		if (sock == INVALID_SOCKET) {
			return false;
		}
		sockaddr_in local{};
		local.sin_family = AF_INET;
		local.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
		local.sin_port = 0;
		if (bind(sock, reinterpret_cast<sockaddr*>(&local), sizeof(local)) != 0) {
			return false;
		}
		DWORD timeout = 200;
		setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&timeout), sizeof(timeout));
		dest.sin_family = AF_INET;
		dest.sin_port = htons(port);
		return inet_pton(AF_INET, host, &dest.sin_addr) == 1;
	}

	void close()
	{
		if (sock != INVALID_SOCKET) {
			closesocket(sock);
			sock = INVALID_SOCKET;
		}
	}

	bool send(const void* data, int len)
	{
		return sendto(sock, static_cast<const char*>(data), len, 0, reinterpret_cast<sockaddr*>(&dest), sizeof(dest)) == len;
	}

	int recv(char* buf, int cap)
	{
		sockaddr_in from{};
		int fromLen = sizeof(from);
		return recvfrom(sock, buf, cap, 0, reinterpret_cast<sockaddr*>(&from), &fromLen);
	}
};

struct Inbox {
	cmp::Welcome welcome{};
	bool haveWelcome{ false };
	cmp::WorldSnapshot snap{};
	bool haveSnap{ false };
	cmp::Reject reject{};
	bool haveReject{ false };
	cmp::SessionInfo session{};
	bool haveSession{ false };
	std::vector<cmp::PlayerPose> poses;
	std::vector<cmp::Bye> byes;
	cmp::BlobAssembly appearAsm;
	cmp::BlobAssembly invAsm;
	std::vector<std::uint8_t> appearance;
	std::vector<std::uint8_t> inventory;
	bool haveAppearance{ false };
	bool haveInventory{ false };
};

void pump(UdpClient& c, Inbox& in, int n)
{
	for (int i = 0; i < n; ++i) {
		char buf[512]{};
		const int got = c.recv(buf, sizeof(buf));
		if (got < static_cast<int>(sizeof(cmp::Header))) {
			continue;
		}
		cmp::Header header{};
		std::memcpy(&header, buf, sizeof(header));
		if (!cmp::header_ok(header, static_cast<std::size_t>(got))) {
			continue;
		}
		const auto type = static_cast<cmp::Msg>(header.type);
		if (type == cmp::Msg::Welcome && got >= static_cast<int>(sizeof(cmp::Welcome))) {
			std::memcpy(&in.welcome, buf, sizeof(in.welcome));
			in.haveWelcome = true;
		} else if (type == cmp::Msg::WorldSnapshot && got >= static_cast<int>(sizeof(cmp::WorldSnapshot))) {
			std::memcpy(&in.snap, buf, sizeof(in.snap));
			in.haveSnap = true;
		} else if (type == cmp::Msg::Reject && got >= static_cast<int>(sizeof(cmp::Reject))) {
			std::memcpy(&in.reject, buf, sizeof(in.reject));
			in.haveReject = true;
		} else if (type == cmp::Msg::SessionInfo && got >= static_cast<int>(sizeof(cmp::SessionInfo))) {
			std::memcpy(&in.session, buf, sizeof(in.session));
			in.haveSession = true;
		} else if (type == cmp::Msg::PlayerPose && got >= static_cast<int>(sizeof(cmp::PlayerPose))) {
			cmp::PlayerPose pose{};
			std::memcpy(&pose, buf, sizeof(pose));
			in.poses.push_back(pose);
		} else if (type == cmp::Msg::Bye && got >= static_cast<int>(sizeof(cmp::Bye))) {
			cmp::Bye bye{};
			std::memcpy(&bye, buf, sizeof(bye));
			in.byes.push_back(bye);
		} else if (type == cmp::Msg::AppearanceChunk) {
			std::vector<std::uint8_t> blob;
			if (cmp::assemble_blob_chunk(
					in.appearAsm,
					std::span<const std::uint8_t>(reinterpret_cast<const std::uint8_t*>(buf), static_cast<std::size_t>(got)),
					blob)
				== cmp::AssembleStatus::Complete) {
				in.appearance = std::move(blob);
				in.haveAppearance = true;
			}
		} else if (type == cmp::Msg::InventoryChunk) {
			std::vector<std::uint8_t> blob;
			if (cmp::assemble_blob_chunk(
					in.invAsm,
					std::span<const std::uint8_t>(reinterpret_cast<const std::uint8_t*>(buf), static_cast<std::size_t>(got)),
					blob)
				== cmp::AssembleStatus::Complete) {
				in.inventory = std::move(blob);
				in.haveInventory = true;
			}
		}
	}
}

void wait_for(UdpClient& c, Inbox& in, bool Inbox::* flag, int spins = 25)
{
	for (int i = 0; i < spins && !(in.*flag); ++i) {
		pump(c, in, 8);
	}
}

cmp::Hello hello_ok(const char* name, const char* key)
{
	return cmp::make_hello(
		name,
		key,
		true,
		cmp::kCommonwealthWorldspace,
		0.0f,
		10.0f,
		0,
		cmp::kSanctuaryX,
		cmp::kSanctuaryY,
		cmp::kSanctuaryZ,
		false);
}

cmp::Hello hello_at(
	const char* name,
	const char* key,
	std::uint32_t loc,
	float x,
	float y,
	float z,
	bool interior = false,
	float hour = 10.0f)
{
	return cmp::make_hello(name, key, true, loc, 0.0f, hour, 0, x, y, z, interior);
}

bool send_chunks(UdpClient& c, cmp::Msg type, std::uint32_t peerId, const std::vector<std::uint8_t>& blob)
{
	std::vector<std::vector<std::uint8_t>> packets;
	if (!cmp::split_blob_chunks(type, peerId, blob, packets)) {
		return false;
	}
	for (const auto& pkt : packets) {
		if (!c.send(pkt.data(), static_cast<int>(pkt.size()))) {
			return false;
		}
	}
	return true;
}

std::vector<std::uint8_t> dummy_appear()
{
	cmp::BlobWriter w;
	w.u32(0x45505041);
	w.u16(1);
	w.u8(0);
	w.u8(0);
	return w.bytes;
}

std::vector<std::uint8_t> dummy_inv()
{
	cmp::InventorySheet sheet;
	cmp::copy_cstr(sheet.name, sizeof(sheet.name), "Tester");
	sheet.sex = 0;
	sheet.race = cmp::pack_form_id(0x00013746, "Fallout4.esm", false);
	sheet.worn.push_back(cmp::pack_form_id(0x000A183B, "Fallout4.esm", false));
	sheet.stacks.push_back({ cmp::pack_form_id(0x00033102, "Fallout4.esm", false), 4 });
	std::vector<std::uint8_t> out;
	cmp::encode_inventory_sheet(sheet, out);
	return out;
}

int case_reject_not_in_world(const Options& opt)
{
	UdpClient c;
	CHECK(c.open(opt.host.c_str(), opt.port));
	const auto hello = cmp::make_hello("x", "k", false, 0, 0, 0, 0, 0, 0, 0, false);
	CHECK(c.send(&hello, sizeof(hello)));
	Inbox in;
	wait_for(c, in, &Inbox::haveReject);
	CHECK(in.haveReject);
	CHECK(in.reject.reason == static_cast<std::uint32_t>(cmp::RejectReason::NotInWorld));
	c.close();
	return 0;
}

int case_reject_protocol(const Options& opt)
{
	UdpClient c;
	CHECK(c.open(opt.host.c_str(), opt.port));
	auto hello = hello_ok("x", "k");
	hello.protocol = 2;
	hello.header.version = 2;
	CHECK(c.send(&hello, sizeof(hello)));
	Inbox in;
	wait_for(c, in, &Inbox::haveReject);
	CHECK(in.haveReject);
	CHECK(in.reject.reason == static_cast<std::uint32_t>(cmp::RejectReason::Protocol));
	c.close();
	return 0;
}

int case_reject_full(const Options& opt)
{
	UdpClient a;
	UdpClient b;
	CHECK(a.open(opt.host.c_str(), opt.port));
	CHECK(b.open(opt.host.c_str(), opt.port));
	const auto ha = hello_ok("A", "full-a");
	CHECK(a.send(&ha, sizeof(ha)));
	Inbox ia;
	wait_for(a, ia, &Inbox::haveWelcome);
	CHECK(ia.haveWelcome);
	const auto hb = hello_ok("B", "full-b");
	CHECK(b.send(&hb, sizeof(hb)));
	Inbox ib;
	wait_for(b, ib, &Inbox::haveReject);
	CHECK(ib.haveReject);
	CHECK(ib.reject.reason == static_cast<std::uint32_t>(cmp::RejectReason::Full));
	a.close();
	b.close();
	return 0;
}

int case_new_player_spawn(const Options& opt)
{
	UdpClient c;
	CHECK(c.open(opt.host.c_str(), opt.port));
	const auto hello = hello_ok("host", "new-key-1");
	CHECK(c.send(&hello, sizeof(hello)));
	Inbox in;
	wait_for(c, in, &Inbox::haveWelcome);
	wait_for(c, in, &Inbox::haveSnap);
	CHECK(in.haveWelcome);
	CHECK(in.welcome.isNewPlayer == 1);
	CHECK(in.welcome.isHost == 1);
	CHECK(in.welcome.fakePeerId == 0);
	CHECK(in.haveSnap);
	CHECK(in.snap.isNewPlayer == 1);
	CHECK(nearly(in.snap.placeX, cmp::kSanctuaryX));
	CHECK(nearly(in.snap.placeY, cmp::kSanctuaryY));
	CHECK(nearly(in.snap.placeZ, cmp::kSanctuaryZ));
	c.close();
	return 0;
}

int case_returning_pose(const Options& opt)
{
	UdpClient c;
	CHECK(c.open(opt.host.c_str(), opt.port));
	const auto hello = hello_ok("back", "return-key");
	CHECK(c.send(&hello, sizeof(hello)));
	Inbox first;
	wait_for(c, first, &Inbox::haveWelcome);
	CHECK(first.haveWelcome);
	CHECK(first.welcome.isNewPlayer == 1);

	const float x = 1111.0f;
	const float y = 2222.0f;
	const float z = 3333.0f;
	const auto pose = cmp::make_pose(first.welcome.peerId, cmp::kCommonwealthWorldspace, x, y, z, 1.2f);
	CHECK(c.send(&pose, sizeof(pose)));
	std::this_thread::sleep_for(std::chrono::milliseconds(50));
	const auto bye = cmp::make_bye(first.welcome.peerId);
	CHECK(c.send(&bye, sizeof(bye)));
	std::this_thread::sleep_for(std::chrono::milliseconds(80));

	CHECK(c.send(&hello, sizeof(hello)));
	Inbox second;
	wait_for(c, second, &Inbox::haveWelcome);
	wait_for(c, second, &Inbox::haveSnap);
	CHECK(second.haveWelcome);
	CHECK(second.welcome.isNewPlayer == 0);
	CHECK(second.haveSnap);
	CHECK(second.snap.isNewPlayer == 0);
	CHECK(nearly(second.snap.placeX, x));
	CHECK(nearly(second.snap.placeY, y));
	CHECK(nearly(second.snap.placeZ, z));
	c.close();
	return 0;
}

int case_guest_meets_host(const Options& opt)
{
	UdpClient host;
	UdpClient guest;
	CHECK(host.open(opt.host.c_str(), opt.port));
	CHECK(guest.open(opt.host.c_str(), opt.port));
	const auto hh = hello_ok("Host", "meet-host");
	CHECK(host.send(&hh, sizeof(hh)));
	Inbox ih;
	wait_for(host, ih, &Inbox::haveWelcome);
	CHECK(ih.haveWelcome);
	CHECK(ih.welcome.isHost == 1);

	const float x = 5000.0f;
	const float y = 6000.0f;
	const float z = 7000.0f;
	const auto pose = cmp::make_pose(ih.welcome.peerId, cmp::kCommonwealthWorldspace, x, y, z, 0.4f);
	CHECK(host.send(&pose, sizeof(pose)));
	std::this_thread::sleep_for(std::chrono::milliseconds(80));

	const auto hg = hello_ok("Guest", "meet-guest");
	CHECK(guest.send(&hg, sizeof(hg)));
	Inbox ig;
	wait_for(guest, ig, &Inbox::haveWelcome);
	wait_for(guest, ig, &Inbox::haveSnap);
	CHECK(ig.haveWelcome);
	CHECK(ig.welcome.isHost == 0);
	CHECK(ig.welcome.isNewPlayer == 1);
	CHECK(ig.haveSnap);
	CHECK(ig.snap.isNewPlayer == 1);
	CHECK(ig.snap.placeLocationFormId == cmp::kCommonwealthWorldspace);
	CHECK(nearly(ig.snap.placeX, x));
	CHECK(nearly(ig.snap.placeY, y));
	CHECK(nearly(ig.snap.placeZ, z));
	CHECK(!nearly(ig.snap.placeX, cmp::kSanctuaryX));
	host.close();
	guest.close();
	return 0;
}

int case_two_clients_relay(const Options& opt)
{
	UdpClient a;
	UdpClient b;
	CHECK(a.open(opt.host.c_str(), opt.port));
	CHECK(b.open(opt.host.c_str(), opt.port));
	const auto ha = hello_ok("A", "relay-a");
	const auto hb = hello_ok("B", "relay-b");
	CHECK(a.send(&ha, sizeof(ha)));
	Inbox ia;
	wait_for(a, ia, &Inbox::haveWelcome);
	CHECK(ia.haveWelcome);
	CHECK(b.send(&hb, sizeof(hb)));
	Inbox ib;
	wait_for(b, ib, &Inbox::haveWelcome);
	CHECK(ib.haveWelcome);

	const auto pose = cmp::make_pose(ia.welcome.peerId, cmp::kCommonwealthWorldspace, 50.0f, 60.0f, 70.0f, 0.0f);
	CHECK(a.send(&pose, sizeof(pose)));
	CHECK(send_chunks(a, cmp::Msg::AppearanceChunk, ia.welcome.peerId, dummy_appear()));
	CHECK(send_chunks(a, cmp::Msg::InventoryChunk, ia.welcome.peerId, dummy_inv()));

	const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
	while (std::chrono::steady_clock::now() < deadline) {
		pump(b, ib, 8);
		bool gotPose = false;
		for (const auto& p : ib.poses) {
			if (p.peerId == ia.welcome.peerId && nearly(p.x, 50.0f) && nearly(p.y, 60.0f)) {
				gotPose = true;
			}
		}
		if (gotPose && ib.haveAppearance && ib.haveInventory) {
			break;
		}
	}
	bool gotPose = false;
	for (const auto& p : ib.poses) {
		if (p.peerId == ia.welcome.peerId) {
			gotPose = true;
		}
	}
	CHECK(gotPose);
	CHECK(ib.haveAppearance);
	CHECK(ib.haveInventory);
	cmp::InventorySheet sheet;
	CHECK(cmp::decode_inventory_sheet(ib.inventory, sheet));
	CHECK(std::string_view(sheet.name) == "Tester");
	a.close();
	b.close();
	return 0;
}

int case_persist_files(const Options& opt)
{
	CHECK(!opt.sessionDir.empty());
	UdpClient c;
	CHECK(c.open(opt.host.c_str(), opt.port));
	const auto hello = hello_ok("save", "persist-key");
	CHECK(c.send(&hello, sizeof(hello)));
	Inbox in;
	wait_for(c, in, &Inbox::haveWelcome);
	CHECK(in.haveWelcome);
	const auto pose = cmp::make_pose(in.welcome.peerId, cmp::kCommonwealthWorldspace, 9.0f, 8.0f, 7.0f, 0.0f);
	CHECK(c.send(&pose, sizeof(pose)));
	CHECK(send_chunks(c, cmp::Msg::InventoryChunk, in.welcome.peerId, dummy_inv()));
	std::this_thread::sleep_for(std::chrono::milliseconds(200));
	const auto bye = cmp::make_bye(in.welcome.peerId);
	CHECK(c.send(&bye, sizeof(bye)));
	std::this_thread::sleep_for(std::chrono::milliseconds(120));

	const fs::path dir = opt.sessionDir;
	CHECK(fs::exists(dir / "world.json"));
	CHECK(fs::exists(dir / "players" / "persist-key.json"));
	CHECK(fs::exists(dir / "players" / "persist-key.inventory.bin"));
	c.close();
	return 0;
}

int case_host_handoff(const Options& opt)
{
	UdpClient a;
	UdpClient b;
	UdpClient c;
	CHECK(a.open(opt.host.c_str(), opt.port));
	CHECK(b.open(opt.host.c_str(), opt.port));
	CHECK(c.open(opt.host.c_str(), opt.port));
	const auto ha = hello_ok("A", "host-a");
	const auto hb = hello_ok("B", "host-b");
	const auto hc = hello_ok("C", "host-c");
	CHECK(a.send(&ha, sizeof(ha)));
	Inbox ia;
	wait_for(a, ia, &Inbox::haveWelcome);
	CHECK(ia.haveWelcome);
	CHECK(ia.welcome.isHost == 1);
	CHECK(b.send(&hb, sizeof(hb)));
	Inbox ib;
	wait_for(b, ib, &Inbox::haveWelcome);
	CHECK(ib.haveWelcome);
	CHECK(ib.welcome.isHost == 0);

	const auto bye = cmp::make_bye(ia.welcome.peerId);
	CHECK(a.send(&bye, sizeof(bye)));
	std::this_thread::sleep_for(std::chrono::milliseconds(80));

	CHECK(c.send(&hc, sizeof(hc)));
	Inbox ic;
	wait_for(c, ic, &Inbox::haveWelcome);
	wait_for(c, ic, &Inbox::haveSnap);
	CHECK(ic.haveSnap);
	CHECK(ic.snap.hostPeerId == ib.welcome.peerId);
	a.close();
	b.close();
	c.close();
	return 0;
}

int case_junk_and_unknown(const Options& opt)
{
	UdpClient c;
	CHECK(c.open(opt.host.c_str(), opt.port));

	char tiny[] = { 'x' };
	CHECK(c.send(tiny, 1));
	char hdr[8]{};
	CHECK(c.send(hdr, sizeof(hdr)));

	cmp::Header lie{};
	std::memcpy(lie.magic, cmp::kMagic, 4);
	lie.type = static_cast<std::uint8_t>(cmp::Msg::Hello);
	lie.version = cmp::kProtocolVersion;
	lie.size = 400;
	CHECK(c.send(&lie, sizeof(lie)));

	const auto pose = cmp::make_pose(99, cmp::kCommonwealthWorldspace, 1, 2, 3, 0);
	CHECK(c.send(&pose, sizeof(pose)));
	const auto bye = cmp::make_bye(99);
	CHECK(c.send(&bye, sizeof(bye)));
	CHECK(send_chunks(c, cmp::Msg::InventoryChunk, 99, dummy_inv()));
	std::this_thread::sleep_for(std::chrono::milliseconds(80));

	auto badVer = hello_ok("x", "junk-ver");
	badVer.pluginVersion = 1;
	CHECK(c.send(&badVer, sizeof(badVer)));
	Inbox rejected;
	wait_for(c, rejected, &Inbox::haveReject);
	CHECK(rejected.haveReject);
	CHECK(rejected.reject.reason == static_cast<std::uint32_t>(cmp::RejectReason::PluginVersion));

	auto emptyKey = hello_ok("x", "@@@");
	CHECK(c.send(&emptyKey, sizeof(emptyKey)));
	Inbox emptyJoin;
	wait_for(c, emptyJoin, &Inbox::haveWelcome);
	CHECK(emptyJoin.haveWelcome);

	UdpClient d;
	CHECK(d.open(opt.host.c_str(), opt.port));
	const auto ok = hello_ok("ok", "after-junk");
	CHECK(d.send(&ok, sizeof(ok)));
	Inbox live;
	wait_for(d, live, &Inbox::haveWelcome);
	CHECK(live.haveWelcome);
	CHECK(live.welcome.peerId != 0);
	c.close();
	d.close();
	return 0;
}

int case_fake_off_at_two(const Options& opt)
{
	UdpClient a;
	UdpClient b;
	CHECK(a.open(opt.host.c_str(), opt.port));
	CHECK(b.open(opt.host.c_str(), opt.port));
	const auto ha = hello_ok("A", "fake-a");
	CHECK(a.send(&ha, sizeof(ha)));
	Inbox ia;
	wait_for(a, ia, &Inbox::haveWelcome);
	CHECK(ia.haveWelcome);
	CHECK(ia.welcome.fakePeerId == cmp::kFakePeerId);

	const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
	bool sawFake = false;
	while (std::chrono::steady_clock::now() < deadline && !sawFake) {
		pump(a, ia, 8);
		for (const auto& p : ia.poses) {
			if (p.peerId == cmp::kFakePeerId) {
				sawFake = true;
			}
		}
	}
	CHECK(sawFake);

	const auto hb = hello_ok("B", "fake-b");
	CHECK(b.send(&hb, sizeof(hb)));
	Inbox ib;
	wait_for(b, ib, &Inbox::haveWelcome);
	CHECK(ib.haveWelcome);

	bool sawBye = false;
	const auto until = std::chrono::steady_clock::now() + std::chrono::seconds(2);
	while (std::chrono::steady_clock::now() < until && !sawBye) {
		pump(a, ia, 8);
		for (const auto& bye : ia.byes) {
			if (bye.peerId == cmp::kFakePeerId) {
				sawBye = true;
			}
		}
	}
	CHECK(sawBye);
	a.close();
	b.close();
	return 0;
}

int case_query_empty(const Options& opt)
{
	UdpClient c;
	CHECK(c.open(opt.host.c_str(), opt.port));
	const auto q = cmp::make_session_query();
	CHECK(c.send(&q, sizeof(q)));
	Inbox in;
	wait_for(c, in, &Inbox::haveSession);
	CHECK(in.haveSession);
	CHECK(in.session.haveHost == 0);
	CHECK(in.session.clientCount == 0);
	CHECK(in.session.maxPlayers >= 1);
	CHECK(in.session.serverName[0] != '\0');
	c.close();
	return 0;
}

int case_query_with_host(const Options& opt)
{
	UdpClient host;
	CHECK(host.open(opt.host.c_str(), opt.port));
	const auto hh = hello_ok("Host", "query-host");
	CHECK(host.send(&hh, sizeof(hh)));
	Inbox ih;
	wait_for(host, ih, &Inbox::haveWelcome);
	CHECK(ih.haveWelcome);
	const float x = -12000.0f;
	const float y = 44000.0f;
	const float z = 8000.0f;
	const auto pose = cmp::make_pose(ih.welcome.peerId, cmp::kCommonwealthWorldspace, x, y, z, 0.2f);
	CHECK(host.send(&pose, sizeof(pose)));
	std::this_thread::sleep_for(std::chrono::milliseconds(50));

	UdpClient q;
	CHECK(q.open(opt.host.c_str(), opt.port));
	const auto query = cmp::make_session_query();
	CHECK(q.send(&query, sizeof(query)));
	Inbox iq;
	wait_for(q, iq, &Inbox::haveSession);
	CHECK(iq.haveSession);
	CHECK(iq.session.haveHost == 1);
	CHECK(iq.session.hostPeerId == ih.welcome.peerId);
	CHECK(iq.session.hostLocationFormId == cmp::kCommonwealthWorldspace);
	CHECK(nearly(iq.session.hostX, x));
	CHECK(nearly(iq.session.hostY, y));
	CHECK(nearly(iq.session.hostZ, z));
	CHECK(iq.session.hostInterior == 0);
	CHECK(iq.session.clientCount >= 1);
	CHECK(iq.session.maxPlayers >= 1);
	CHECK(iq.session.serverName[0] != '\0');
	q.close();
	host.close();
	return 0;
}

int case_require_host_no_host(const Options& opt)
{
	UdpClient c;
	CHECK(c.open(opt.host.c_str(), opt.port));
	auto hello = hello_ok("Guest", "need-host");
	hello.flags = cmp::kHelloFlagRequireHost;
	CHECK(c.send(&hello, sizeof(hello)));
	Inbox in;
	wait_for(c, in, &Inbox::haveReject);
	CHECK(in.haveReject);
	CHECK(in.reject.reason == static_cast<std::uint32_t>(cmp::RejectReason::NoHost));
	CHECK(!in.haveWelcome);
	c.close();
	return 0;
}

int case_require_host_at_host(const Options& opt)
{
	UdpClient host;
	CHECK(host.open(opt.host.c_str(), opt.port));
	const auto hh = hello_ok("Host", "rh-host");
	CHECK(host.send(&hh, sizeof(hh)));
	Inbox ih;
	wait_for(host, ih, &Inbox::haveWelcome);
	CHECK(ih.haveWelcome);
	CHECK(ih.welcome.isHost == 1);
	const float x = 22000.0f;
	const float y = -33000.0f;
	const float z = 9100.0f;
	const auto pose = cmp::make_pose(ih.welcome.peerId, cmp::kCommonwealthWorldspace, x, y, z, 1.1f);
	CHECK(host.send(&pose, sizeof(pose)));
	std::this_thread::sleep_for(std::chrono::milliseconds(50));

	UdpClient guest;
	CHECK(guest.open(opt.host.c_str(), opt.port));
	auto first = hello_ok("Back", "rh-return");
	CHECK(guest.send(&first, sizeof(first)));
	Inbox ig1;
	wait_for(guest, ig1, &Inbox::haveWelcome);
	CHECK(ig1.haveWelcome);
	const auto oldPose = cmp::make_pose(ig1.welcome.peerId, cmp::kCommonwealthWorldspace, 1111.0f, 2222.0f, 3333.0f, 0.0f);
	CHECK(guest.send(&oldPose, sizeof(oldPose)));
	std::this_thread::sleep_for(std::chrono::milliseconds(50));
	const auto bye = cmp::make_bye(ig1.welcome.peerId);
	CHECK(guest.send(&bye, sizeof(bye)));
	std::this_thread::sleep_for(std::chrono::milliseconds(80));

	auto req = hello_ok("Back", "rh-return");
	req.flags = cmp::kHelloFlagRequireHost;
	CHECK(guest.send(&req, sizeof(req)));
	Inbox ig2;
	wait_for(guest, ig2, &Inbox::haveWelcome);
	wait_for(guest, ig2, &Inbox::haveSnap);
	CHECK(ig2.haveWelcome);
	CHECK(ig2.welcome.isHost == 0);
	CHECK(ig2.welcome.peerId != ih.welcome.peerId);
	CHECK(ig2.haveSnap);
	CHECK(nearly(ig2.snap.placeX, x + cmp::kGuestSpawnOffsetX));
	CHECK(nearly(ig2.snap.placeY, y));
	CHECK(nearly(ig2.snap.placeZ, z));
	CHECK(!nearly(ig2.snap.placeX, 1111.0f));

	guest.close();
	host.close();
	return 0;
}

int case_session_branding(const Options& opt)
{
	UdpClient c;
	CHECK(c.open(opt.host.c_str(), opt.port));
	const auto q = cmp::make_session_query();
	CHECK(c.send(&q, sizeof(q)));
	Inbox in;
	wait_for(c, in, &Inbox::haveSession);
	CHECK(in.haveSession);
	CHECK(std::string_view(in.session.serverName) == "BrandSrv");
	CHECK(std::string_view(in.session.motd) == "BrandMotd");
	CHECK(in.session.maxPlayers == 12);
	c.close();
	return 0;
}

int case_host_not_streaming(const Options& opt)
{
	UdpClient host;
	CHECK(host.open(opt.host.c_str(), opt.port));
	const auto hh = hello_at("Host", "hn-host", 0x0001A26F, 10.0f, 20.0f, 30.0f, true);
	CHECK(host.send(&hh, sizeof(hh)));
	Inbox ih;
	wait_for(host, ih, &Inbox::haveWelcome);
	CHECK(ih.haveWelcome);

	UdpClient guest;
	CHECK(guest.open(opt.host.c_str(), opt.port));
	auto hg = hello_ok("Guest", "hn-guest");
	hg.flags = cmp::kHelloFlagRequireHost;
	CHECK(guest.send(&hg, sizeof(hg)));
	Inbox ig;
	wait_for(guest, ig, &Inbox::haveReject);
	CHECK(ig.haveReject);
	CHECK(ig.reject.reason == static_cast<std::uint32_t>(cmp::RejectReason::HostNotStreaming));
	CHECK(!ig.haveWelcome);
	guest.close();
	host.close();
	return 0;
}

int case_query_interior(const Options& opt)
{
	UdpClient host;
	CHECK(host.open(opt.host.c_str(), opt.port));
	const auto hh = hello_at("Host", "qi-host", 0x0002BE8A, 100.0f, 200.0f, 300.0f, true);
	CHECK(host.send(&hh, sizeof(hh)));
	Inbox ih;
	wait_for(host, ih, &Inbox::haveWelcome);
	CHECK(ih.haveWelcome);

	UdpClient q;
	CHECK(q.open(opt.host.c_str(), opt.port));
	const auto query = cmp::make_session_query();
	CHECK(q.send(&query, sizeof(query)));
	Inbox iq;
	wait_for(q, iq, &Inbox::haveSession);
	CHECK(iq.haveSession);
	CHECK(iq.session.haveHost == 1);
	CHECK(iq.session.hostInterior == 1);
	CHECK(iq.session.hostLocationFormId == 0x0002BE8A);
	q.close();
	host.close();
	return 0;
}

int case_returning_mismatch_world(const Options& opt)
{
	UdpClient host;
	CHECK(host.open(opt.host.c_str(), opt.port));
	const auto hh = hello_ok("Host", "rw-host");
	CHECK(host.send(&hh, sizeof(hh)));
	Inbox ih;
	wait_for(host, ih, &Inbox::haveWelcome);
	CHECK(ih.haveWelcome);

	UdpClient guest;
	CHECK(guest.open(opt.host.c_str(), opt.port));
	const auto hg = hello_ok("Guest", "rw-guest");
	CHECK(guest.send(&hg, sizeof(hg)));
	Inbox ig1;
	wait_for(guest, ig1, &Inbox::haveWelcome);
	CHECK(ig1.haveWelcome);
	const auto oldPose = cmp::make_pose(ig1.welcome.peerId, 0x0001A26F, 1111.0f, 2222.0f, 3333.0f, 0.0f);
	CHECK(guest.send(&oldPose, sizeof(oldPose)));
	std::this_thread::sleep_for(std::chrono::milliseconds(50));
	const auto bye = cmp::make_bye(ig1.welcome.peerId);
	CHECK(guest.send(&bye, sizeof(bye)));
	std::this_thread::sleep_for(std::chrono::milliseconds(80));

	CHECK(guest.send(&hg, sizeof(hg)));
	Inbox ig2;
	wait_for(guest, ig2, &Inbox::haveWelcome);
	wait_for(guest, ig2, &Inbox::haveSnap);
	CHECK(ig2.haveWelcome);
	CHECK(ig2.welcome.isNewPlayer == 0);
	CHECK(ig2.haveSnap);
	CHECK(nearly(ig2.snap.placeX, cmp::kSanctuaryX));
	CHECK(nearly(ig2.snap.placeY, cmp::kSanctuaryY));
	CHECK(nearly(ig2.snap.placeZ, cmp::kSanctuaryZ));
	CHECK(!nearly(ig2.snap.placeX, 1111.0f));
	guest.close();
	host.close();
	return 0;
}

int case_bye_relay(const Options& opt)
{
	UdpClient a;
	UdpClient b;
	CHECK(a.open(opt.host.c_str(), opt.port));
	CHECK(b.open(opt.host.c_str(), opt.port));
	const auto ha = hello_ok("A", "bye-a");
	CHECK(a.send(&ha, sizeof(ha)));
	Inbox ia;
	wait_for(a, ia, &Inbox::haveWelcome);
	CHECK(ia.haveWelcome);
	const auto hb = hello_ok("B", "bye-b");
	CHECK(b.send(&hb, sizeof(hb)));
	Inbox ib;
	wait_for(b, ib, &Inbox::haveWelcome);
	CHECK(ib.haveWelcome);

	const auto bye = cmp::make_bye(ia.welcome.peerId);
	CHECK(a.send(&bye, sizeof(bye)));
	bool sawBye = false;
	const auto until = std::chrono::steady_clock::now() + std::chrono::seconds(2);
	while (std::chrono::steady_clock::now() < until && !sawBye) {
		pump(b, ib, 8);
		for (const auto& msg : ib.byes) {
			if (msg.peerId == ia.welcome.peerId) {
				sawBye = true;
			}
		}
	}
	CHECK(sawBye);
	a.close();
	b.close();
	return 0;
}

int case_hello_again(const Options& opt)
{
	UdpClient c;
	CHECK(c.open(opt.host.c_str(), opt.port));
	const auto hello = hello_ok("Again", "hello-again");
	CHECK(c.send(&hello, sizeof(hello)));
	Inbox first;
	wait_for(c, first, &Inbox::haveWelcome);
	CHECK(first.haveWelcome);
	const auto peer = first.welcome.peerId;

	CHECK(c.send(&hello, sizeof(hello)));
	Inbox second;
	wait_for(c, second, &Inbox::haveWelcome);
	CHECK(second.haveWelcome);
	CHECK(second.welcome.peerId == peer);
	c.close();
	return 0;
}

int case_interest_cull(const Options& opt)
{
	UdpClient a;
	UdpClient b;
	CHECK(a.open(opt.host.c_str(), opt.port));
	CHECK(b.open(opt.host.c_str(), opt.port));
	const auto ha = hello_ok("A", "int-a");
	CHECK(a.send(&ha, sizeof(ha)));
	Inbox ia;
	wait_for(a, ia, &Inbox::haveWelcome);
	CHECK(ia.haveWelcome);
	const auto hb = hello_ok("B", "int-b");
	CHECK(b.send(&hb, sizeof(hb)));
	Inbox ib;
	wait_for(b, ib, &Inbox::haveWelcome);
	CHECK(ib.haveWelcome);
	pump(b, ib, 16);
	ib.poses.clear();

	const auto farPose = cmp::make_pose(
		ia.welcome.peerId, cmp::kCommonwealthWorldspace, cmp::kSanctuaryX + 10000.0f, cmp::kSanctuaryY, cmp::kSanctuaryZ, 0);
	CHECK(a.send(&farPose, sizeof(farPose)));
	std::this_thread::sleep_for(std::chrono::milliseconds(120));
	pump(b, ib, 16);
	bool sawFar = false;
	for (const auto& p : ib.poses) {
		if (p.peerId == ia.welcome.peerId && nearly(p.x, cmp::kSanctuaryX + 10000.0f)) {
			sawFar = true;
		}
	}
	CHECK(!sawFar);

	const auto nearPose = cmp::make_pose(
		ia.welcome.peerId, cmp::kCommonwealthWorldspace, cmp::kSanctuaryX + 10.0f, cmp::kSanctuaryY, cmp::kSanctuaryZ, 0);
	CHECK(a.send(&nearPose, sizeof(nearPose)));
	bool sawNear = false;
	const auto until = std::chrono::steady_clock::now() + std::chrono::seconds(2);
	while (std::chrono::steady_clock::now() < until && !sawNear) {
		pump(b, ib, 8);
		for (const auto& p : ib.poses) {
			if (p.peerId == ia.welcome.peerId && nearly(p.x, cmp::kSanctuaryX + 10.0f)) {
				sawNear = true;
			}
		}
	}
	CHECK(sawNear);
	a.close();
	b.close();
	return 0;
}

int case_persist_appearance(const Options& opt)
{
	CHECK(!opt.sessionDir.empty());
	UdpClient c;
	CHECK(c.open(opt.host.c_str(), opt.port));
	const auto hello = hello_ok("look", "appear-key");
	CHECK(c.send(&hello, sizeof(hello)));
	Inbox in;
	wait_for(c, in, &Inbox::haveWelcome);
	CHECK(in.haveWelcome);
	CHECK(send_chunks(c, cmp::Msg::AppearanceChunk, in.welcome.peerId, dummy_appear()));
	std::this_thread::sleep_for(std::chrono::milliseconds(200));
	const auto bye = cmp::make_bye(in.welcome.peerId);
	CHECK(c.send(&bye, sizeof(bye)));
	std::this_thread::sleep_for(std::chrono::milliseconds(120));
	CHECK(fs::exists(fs::path(opt.sessionDir) / "players" / "appear-key.appearance.bin"));
	c.close();
	return 0;
}

int case_snapshot_host_clock(const Options& opt)
{
	UdpClient host;
	CHECK(host.open(opt.host.c_str(), opt.port));
	const auto hh = hello_at(
		"Host",
		"clock-host",
		cmp::kCommonwealthWorldspace,
		cmp::kSanctuaryX,
		cmp::kSanctuaryY,
		cmp::kSanctuaryZ,
		false,
		14.25f);
	CHECK(host.send(&hh, sizeof(hh)));
	Inbox ih;
	wait_for(host, ih, &Inbox::haveWelcome);
	CHECK(ih.haveWelcome);

	UdpClient guest;
	CHECK(guest.open(opt.host.c_str(), opt.port));
	const auto hg = hello_ok("Guest", "clock-guest");
	CHECK(guest.send(&hg, sizeof(hg)));
	Inbox ig;
	wait_for(guest, ig, &Inbox::haveWelcome);
	wait_for(guest, ig, &Inbox::haveSnap);
	CHECK(ig.haveSnap);
	CHECK(nearly(ig.snap.gameHour, 14.25f));
	guest.close();
	host.close();
	return 0;
}

int case_same_key_two_sockets(const Options& opt)
{
	UdpClient a;
	UdpClient b;
	CHECK(a.open(opt.host.c_str(), opt.port));
	CHECK(b.open(opt.host.c_str(), opt.port));
	const auto ha = hello_ok("A", "shared-key");
	CHECK(a.send(&ha, sizeof(ha)));
	Inbox ia;
	wait_for(a, ia, &Inbox::haveWelcome);
	CHECK(ia.haveWelcome);
	const auto hb = hello_ok("B", "shared-key");
	CHECK(b.send(&hb, sizeof(hb)));
	Inbox ib;
	wait_for(b, ib, &Inbox::haveWelcome);
	CHECK(ib.haveWelcome);
	CHECK(ia.welcome.peerId != ib.welcome.peerId);
	a.close();
	b.close();
	return 0;
}

Options parse(int argc, char** argv)
{
	Options o;
	for (int i = 1; i < argc; ++i) {
		const std::string arg = argv[i];
		if (arg == "--case" && i + 1 < argc) {
			o.which = argv[++i];
		} else if (arg == "--host" && i + 1 < argc) {
			o.host = argv[++i];
		} else if (arg == "--port" && i + 1 < argc) {
			o.port = static_cast<std::uint16_t>(std::stoi(argv[++i]));
		} else if (arg == "--session-dir" && i + 1 < argc) {
			o.sessionDir = argv[++i];
		}
	}
	return o;
}

}  // namespace

int main(int argc, char** argv)
{
	const auto opt = parse(argc, argv);
	if (opt.which.empty()) {
		std::cerr << "usage: CommonwealthMP.ServerCases --case NAME --port N [--session-dir PATH]\n";
		return 2;
	}

	WSADATA wsa{};
	if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
		std::cerr << "WSAStartup failed\n";
		return 1;
	}

	if (opt.which == "reject_not_in_world") {
		case_reject_not_in_world(opt);
	} else if (opt.which == "reject_protocol") {
		case_reject_protocol(opt);
	} else if (opt.which == "reject_full") {
		case_reject_full(opt);
	} else if (opt.which == "new_player_spawn") {
		case_new_player_spawn(opt);
	} else if (opt.which == "returning_pose") {
		case_returning_pose(opt);
	} else if (opt.which == "guest_meets_host") {
		case_guest_meets_host(opt);
	} else if (opt.which == "two_clients_relay") {
		case_two_clients_relay(opt);
	} else if (opt.which == "persist_files") {
		case_persist_files(opt);
	} else if (opt.which == "host_handoff") {
		case_host_handoff(opt);
	} else if (opt.which == "fake_off_at_two") {
		case_fake_off_at_two(opt);
	} else if (opt.which == "junk_and_unknown") {
		case_junk_and_unknown(opt);
	} else if (opt.which == "query_empty") {
		case_query_empty(opt);
	} else if (opt.which == "query_with_host") {
		case_query_with_host(opt);
	} else if (opt.which == "require_host_no_host") {
		case_require_host_no_host(opt);
	} else if (opt.which == "require_host_at_host") {
		case_require_host_at_host(opt);
	} else if (opt.which == "session_branding") {
		case_session_branding(opt);
	} else if (opt.which == "host_not_streaming") {
		case_host_not_streaming(opt);
	} else if (opt.which == "query_interior") {
		case_query_interior(opt);
	} else if (opt.which == "returning_mismatch_world") {
		case_returning_mismatch_world(opt);
	} else if (opt.which == "bye_relay") {
		case_bye_relay(opt);
	} else if (opt.which == "hello_again") {
		case_hello_again(opt);
	} else if (opt.which == "interest_cull") {
		case_interest_cull(opt);
	} else if (opt.which == "persist_appearance") {
		case_persist_appearance(opt);
	} else if (opt.which == "snapshot_host_clock") {
		case_snapshot_host_clock(opt);
	} else if (opt.which == "same_key_two_sockets") {
		case_same_key_two_sockets(opt);
	} else {
		std::cerr << "unknown case " << opt.which << "\n";
		WSACleanup();
		return 2;
	}

	WSACleanup();
	if (g_fails) {
		std::cerr << "case " << opt.which << ": " << g_fails << " failed / " << g_checks << " checks\n";
		return 1;
	}
	std::cout << "case " << opt.which << ": " << g_checks << " checks ok\n";
	return 0;
}
