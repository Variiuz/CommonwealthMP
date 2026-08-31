#include <WinSock2.h>
#include <WS2tcpip.h>

#include <chrono>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <string>
#include <thread>

#include "cmp_protocol.hpp"

#pragma comment(lib, "ws2_32.lib")

int main(int argc, char** argv)
{
	const char* host = "127.0.0.1";
	std::uint16_t port = cmp::kDefaultPort;
	if (argc >= 2) {
		host = argv[1];
	}
	if (argc >= 3) {
		port = static_cast<std::uint16_t>(std::stoi(argv[2]));
	}

	WSADATA wsa{};
	if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
		std::cerr << "WSAStartup failed\n";
		return 1;
	}

	SOCKET sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
	if (sock == INVALID_SOCKET) {
		std::cerr << "socket failed\n";
		WSACleanup();
		return 1;
	}

	sockaddr_in dest{};
	dest.sin_family = AF_INET;
	dest.sin_port = htons(port);
	if (inet_pton(AF_INET, host, &dest.sin_addr) != 1) {
		std::cerr << "bad host " << host << "\n";
		closesocket(sock);
		WSACleanup();
		return 2;
	}

	DWORD timeout = 2000;
	setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&timeout), sizeof(timeout));

	const auto hello = cmp::make_hello(
		"probe",
		"probe-key",
		true,
		cmp::kCommonwealthWorldspace,
		0.0f,
		10.0f,
		0,
		cmp::kSanctuaryX,
		cmp::kSanctuaryY,
		cmp::kSanctuaryZ,
		false);
	if (sendto(sock, reinterpret_cast<const char*>(&hello), sizeof(hello), 0, reinterpret_cast<sockaddr*>(&dest), sizeof(dest)) < 0) {
		std::cerr << "send Hello failed WSA " << WSAGetLastError() << "\n";
		closesocket(sock);
		WSACleanup();
		return 3;
	}
	std::cout << "Sent Hello v3 to " << host << ":" << port << " key=" << hello.playerKey << "\n";

	bool gotWelcome = false;
	int poses = 0;
	std::uint32_t myId = 0;
	std::uint32_t fakeId = 0;
	const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(4);

	while (std::chrono::steady_clock::now() < deadline) {
		char buf[512]{};
		sockaddr_in from{};
		int fromLen = sizeof(from);
		const int n = recvfrom(sock, buf, sizeof(buf), 0, reinterpret_cast<sockaddr*>(&from), &fromLen);
		if (n < static_cast<int>(sizeof(cmp::Header))) {
			continue;
		}
		cmp::Header header{};
		std::memcpy(&header, buf, sizeof(header));
		if (!cmp::header_ok(header, static_cast<std::size_t>(n))) {
			continue;
		}
		const auto type = static_cast<cmp::Msg>(header.type);
		if (type == cmp::Msg::Reject && n >= static_cast<int>(sizeof(cmp::Reject))) {
			cmp::Reject reject{};
			std::memcpy(&reject, buf, sizeof(reject));
			reject.message[95] = '\0';
			std::cerr << "Reject " << reject.message << "\n";
			closesocket(sock);
			WSACleanup();
			return 6;
		}
		if (type == cmp::Msg::Welcome && n >= static_cast<int>(sizeof(cmp::Welcome))) {
			cmp::Welcome welcome{};
			std::memcpy(&welcome, buf, sizeof(welcome));
			gotWelcome = true;
			myId = welcome.peerId;
			fakeId = welcome.fakePeerId;
			std::cout << "Welcome peerId=" << myId << " fakePeerId=" << fakeId
					  << " new=" << static_cast<int>(welcome.isNewPlayer)
					  << " host=" << static_cast<int>(welcome.isHost) << "\n";
			continue;
		}
		if (type == cmp::Msg::WorldSnapshot && n >= static_cast<int>(sizeof(cmp::WorldSnapshot))) {
			cmp::WorldSnapshot snap{};
			std::memcpy(&snap, buf, sizeof(snap));
			std::cout << "WorldSnapshot host=" << snap.hostPeerId << " place=("
					  << snap.placeX << "," << snap.placeY << "," << snap.placeZ << ")\n";
			continue;
		}
		if (type == cmp::Msg::PlayerPose && n >= static_cast<int>(sizeof(cmp::PlayerPose))) {
			cmp::PlayerPose pose{};
			std::memcpy(&pose, buf, sizeof(pose));
			++poses;
			std::cout << "PlayerPose peer=" << pose.peerId << " loc=0x" << std::hex << pose.locationFormId << std::dec
					  << " xyz=(" << pose.x << "," << pose.y << "," << pose.z << ")\n";
			if (gotWelcome && poses >= 2) {
				break;
			}
		}
	}

	const auto bye = cmp::make_bye(myId);
	sendto(sock, reinterpret_cast<const char*>(&bye), sizeof(bye), 0, reinterpret_cast<sockaddr*>(&dest), sizeof(dest));

	closesocket(sock);
	WSACleanup();

	if (!gotWelcome) {
		std::cerr << "No Welcome (is CommonwealthMP.Server.exe running?)\n";
		return 4;
	}
	if (fakeId != 0 && poses < 1) {
		std::cerr << "Welcome ok but no fake-peer PlayerPose\n";
		return 5;
	}
	std::cout << "Probe ok\n";
	return 0;
}
