#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <WinSock2.h>
#include <WS2tcpip.h>
#include "udp_win.h"

#pragma comment(lib, "ws2_32.lib")

namespace {

int g_wsa = 0;
SOCKET g_sock = INVALID_SOCKET;
sockaddr_in g_dest{};

}  // namespace

extern "C" int cmp_udp_startup(void)
{
	if (!g_wsa) {
		WSADATA wsa{};
		if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
			return 0;
		}
		g_wsa = 1;
	}
	if (g_sock != INVALID_SOCKET) {
		closesocket(g_sock);
		g_sock = INVALID_SOCKET;
	}
	g_sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
	if (g_sock == INVALID_SOCKET) {
		return 0;
	}
	u_long nonblock = 1;
	ioctlsocket(g_sock, FIONBIO, &nonblock);
	return 1;
}

extern "C" void cmp_udp_shutdown(void)
{
	if (g_sock != INVALID_SOCKET) {
		closesocket(g_sock);
		g_sock = INVALID_SOCKET;
	}
}

extern "C" int cmp_udp_send(const char* host, unsigned short port, const void* data, int len)
{
	if (g_sock == INVALID_SOCKET || !host || !data || len <= 0) {
		return 0;
	}
	g_dest = {};
	g_dest.sin_family = AF_INET;
	g_dest.sin_port = htons(port);
	if (inet_pton(AF_INET, host, &g_dest.sin_addr) != 1) {
		return 0;
	}
	return sendto(g_sock, static_cast<const char*>(data), len, 0, reinterpret_cast<const sockaddr*>(&g_dest), sizeof(g_dest)) == len ? 1 : 0;
}

extern "C" int cmp_udp_recv(void* buf, int maxlen)
{
	if (g_sock == INVALID_SOCKET || !buf || maxlen <= 0) {
		return -1;
	}
	sockaddr_in from{};
	int fromLen = sizeof(from);
	const int n = recvfrom(g_sock, static_cast<char*>(buf), maxlen, 0, reinterpret_cast<sockaddr*>(&from), &fromLen);
	if (n <= 0) {
		return -1;
	}
	return n;
}
