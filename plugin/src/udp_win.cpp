#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <WinSock2.h>
#include <WS2tcpip.h>
#include "udp_win.h"

#include <cstring>
#include <vector>

#pragma comment(lib, "ws2_32.lib")

namespace {

int g_wsa = 0;
SOCKET g_udp = INVALID_SOCKET;
SOCKET g_tcp = INVALID_SOCKET;
sockaddr_in g_dest{};
std::vector<unsigned char> g_tcpRecv;

}  // namespace

extern "C" int cmp_net_startup(void)
{
	if (!g_wsa) {
		WSADATA wsa{};
		if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
			return 0;
		}
		g_wsa = 1;
	}
	if (g_udp != INVALID_SOCKET) {
		closesocket(g_udp);
		g_udp = INVALID_SOCKET;
	}
	g_udp = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
	if (g_udp == INVALID_SOCKET) {
		return 0;
	}
	u_long nonblock = 1;
	ioctlsocket(g_udp, FIONBIO, &nonblock);
	return 1;
}

extern "C" void cmp_net_shutdown(void)
{
	if (g_tcp != INVALID_SOCKET) {
		closesocket(g_tcp);
		g_tcp = INVALID_SOCKET;
	}
	if (g_udp != INVALID_SOCKET) {
		closesocket(g_udp);
		g_udp = INVALID_SOCKET;
	}
	g_tcpRecv.clear();
}

extern "C" int cmp_net_tcp_connected(void)
{
	return g_tcp != INVALID_SOCKET ? 1 : 0;
}

extern "C" int cmp_net_tcp_connect(const char* host, unsigned short port)
{
	if (g_tcp != INVALID_SOCKET) {
		closesocket(g_tcp);
		g_tcp = INVALID_SOCKET;
	}
	g_tcpRecv.clear();
	if (!host) {
		return 0;
	}
	g_tcp = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
	if (g_tcp == INVALID_SOCKET) {
		return 0;
	}
	int nodelay = 1;
	setsockopt(g_tcp, IPPROTO_TCP, TCP_NODELAY, reinterpret_cast<const char*>(&nodelay), sizeof(nodelay));
	sockaddr_in dest{};
	dest.sin_family = AF_INET;
	dest.sin_port = htons(port);
	if (inet_pton(AF_INET, host, &dest.sin_addr) != 1) {
		closesocket(g_tcp);
		g_tcp = INVALID_SOCKET;
		return 0;
	}
	if (connect(g_tcp, reinterpret_cast<sockaddr*>(&dest), sizeof(dest)) != 0) {
		closesocket(g_tcp);
		g_tcp = INVALID_SOCKET;
		return 0;
	}
	u_long nonblock = 1;
	ioctlsocket(g_tcp, FIONBIO, &nonblock);
	g_dest = dest;
	return 1;
}

extern "C" int cmp_net_tcp_send(const void* data, int len)
{
	if (g_tcp == INVALID_SOCKET || !data || len <= 0) {
		return 0;
	}
	const char* p = static_cast<const char*>(data);
	int sent = 0;
	while (sent < len) {
		const int n = send(g_tcp, p + sent, len - sent, 0);
		if (n <= 0) {
			return 0;
		}
		sent += n;
	}
	return 1;
}

extern "C" int cmp_net_tcp_recv_frame(void* buf, int maxlen)
{
	if (g_tcp == INVALID_SOCKET || !buf || maxlen < 12) {
		return -1;
	}
	char tmp[4096];
	for (;;) {
		const int n = recv(g_tcp, tmp, sizeof(tmp), 0);
		if (n > 0) {
			g_tcpRecv.insert(g_tcpRecv.end(), tmp, tmp + n);
		} else if (n == 0) {
			closesocket(g_tcp);
			g_tcp = INVALID_SOCKET;
			return -2;
		} else {
			const int err = WSAGetLastError();
			if (err != WSAEWOULDBLOCK && err != WSAETIMEDOUT) {
				closesocket(g_tcp);
				g_tcp = INVALID_SOCKET;
				return -2;
			}
			break;
		}
	}
	if (g_tcpRecv.size() < 12) {
		return -1;
	}
	unsigned short size = 0;
	std::memcpy(&size, g_tcpRecv.data() + 6, sizeof(size));
	if (size < 12 || size > 65535) {
		g_tcpRecv.clear();
		return -1;
	}
	if (g_tcpRecv.size() < size) {
		return -1;
	}
	if (static_cast<int>(size) > maxlen) {
		g_tcpRecv.erase(g_tcpRecv.begin(), g_tcpRecv.begin() + size);
		return -1;
	}
	std::memcpy(buf, g_tcpRecv.data(), size);
	g_tcpRecv.erase(g_tcpRecv.begin(), g_tcpRecv.begin() + size);
	return static_cast<int>(size);
}

extern "C" int cmp_net_udp_send(const char* host, unsigned short port, const void* data, int len)
{
	if (g_udp == INVALID_SOCKET || !host || !data || len <= 0) {
		return 0;
	}
	sockaddr_in dest{};
	dest.sin_family = AF_INET;
	dest.sin_port = htons(port);
	if (inet_pton(AF_INET, host, &dest.sin_addr) != 1) {
		return 0;
	}
	g_dest = dest;
	return sendto(g_udp, static_cast<const char*>(data), len, 0, reinterpret_cast<const sockaddr*>(&dest), sizeof(dest)) == len ? 1 : 0;
}

extern "C" int cmp_net_udp_recv(void* buf, int maxlen)
{
	if (g_udp == INVALID_SOCKET || !buf || maxlen <= 0) {
		return -1;
	}
	sockaddr_in from{};
	int fromLen = sizeof(from);
	const int n = recvfrom(g_udp, static_cast<char*>(buf), maxlen, 0, reinterpret_cast<sockaddr*>(&from), &fromLen);
	if (n <= 0) {
		return -1;
	}
	return n;
}

// Back-compat shims used during transition if any call sites remain.
extern "C" int cmp_udp_startup(void) { return cmp_net_startup(); }
extern "C" void cmp_udp_shutdown(void) { cmp_net_shutdown(); }
extern "C" int cmp_udp_send(const char* host, unsigned short port, const void* data, int len)
{
	return cmp_net_udp_send(host, port, data, len);
}
extern "C" int cmp_udp_recv(void* buf, int maxlen) { return cmp_net_udp_recv(buf, maxlen); }
