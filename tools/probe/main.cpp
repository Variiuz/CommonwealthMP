#include "cmp_protocol.hpp"
#include "cmp_net.hpp"
#include "cmp_udp.hpp"

#include <chrono>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <string>
#include <vector>

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

	if (!cmp::udp_startup()) {
		std::cerr << "socket startup failed\n";
		return 1;
	}

	CmpSocket tcp = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
	if (tcp == kCmpInvalidSocket) {
		std::cerr << "tcp socket failed\n";
		cmp::udp_cleanup();
		return 2;
	}
	CmpSocket udp = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
	if (udp == kCmpInvalidSocket) {
		std::cerr << "udp socket failed\n";
		cmp::udp_close(tcp);
		cmp::udp_cleanup();
		return 2;
	}

	sockaddr_in dest{};
	dest.sin_family = AF_INET;
	dest.sin_port = htons(port);
	if (inet_pton(AF_INET, host, &dest.sin_addr) != 1) {
		std::cerr << "bad host " << host << "\n";
		cmp::udp_close(tcp);
		cmp::udp_close(udp);
		cmp::udp_cleanup();
		return 2;
	}
	if (connect(tcp, reinterpret_cast<sockaddr*>(&dest), sizeof(dest)) != 0) {
		std::cerr << "tcp connect failed err " << cmp::udp_last_error() << "\n";
		cmp::udp_close(tcp);
		cmp::udp_close(udp);
		cmp::udp_cleanup();
		return 3;
	}
	cmp::tcp_set_nodelay(tcp);
	cmp::udp_set_recv_timeout_ms(tcp, 500);
	cmp::udp_set_recv_timeout_ms(udp, 500);

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
	if (!cmp::tcp_send_msg(tcp, &hello, sizeof(hello))) {
		std::cerr << "send Hello failed\n";
		cmp::udp_close(tcp);
		cmp::udp_close(udp);
		cmp::udp_cleanup();
		return 3;
	}
	std::cout << "Sent Hello v" << static_cast<unsigned>(cmp::kProtocolVersion) << " to " << host << ":" << port
		<< " key=" << hello.playerKey << "\n";

	bool gotWelcome = false;
	int poses = 0;
	std::uint32_t myId = 0;
	std::uint32_t token = 0;
	std::vector<std::uint8_t> tcpRecv;
	const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(4);

	while (std::chrono::steady_clock::now() < deadline) {
		cmp::tcp_recv_append(tcp, tcpRecv);
		std::vector<std::uint8_t> frame;
		while (cmp::tcp_try_extract_frame(tcpRecv, frame)) {
			if (frame.size() < sizeof(cmp::Header)) {
				continue;
			}
			cmp::Header header{};
			std::memcpy(&header, frame.data(), sizeof(header));
			if (!cmp::header_ok(header, frame.size())) {
				continue;
			}
			const auto type = static_cast<cmp::Msg>(header.type);
			if (type == cmp::Msg::Welcome && frame.size() >= sizeof(cmp::Welcome)) {
				cmp::Welcome welcome{};
				std::memcpy(&welcome, frame.data(), sizeof(welcome));
				gotWelcome = true;
				myId = welcome.peerId;
				token = welcome.udpToken;
				std::cout << "Welcome peer=" << myId << " token=" << token << "\n";
				const auto bind = cmp::make_udp_bind(myId, token);
				sendto(udp, reinterpret_cast<const char*>(&bind), sizeof(bind), 0,
					reinterpret_cast<sockaddr*>(&dest), sizeof(dest));
			}
		}
		char buf[512]{};
		sockaddr_in from{};
		CmpSockLen fromLen = sizeof(from);
		const int n = recvfrom(udp, buf, sizeof(buf), 0, reinterpret_cast<sockaddr*>(&from), &fromLen);
		if (n >= static_cast<int>(sizeof(cmp::Header))) {
			cmp::Header header{};
			std::memcpy(&header, buf, sizeof(header));
			if (cmp::header_ok(header, static_cast<std::size_t>(n))
				&& static_cast<cmp::Msg>(header.type) == cmp::Msg::PlayerPose) {
				++poses;
			}
		}
	}

	cmp::udp_close(tcp);
	cmp::udp_close(udp);
	cmp::udp_cleanup();
	if (!gotWelcome) {
		std::cerr << "no Welcome\n";
		return 4;
	}
	std::cout << "ok poses_seen=" << poses << "\n";
	return 0;
}
