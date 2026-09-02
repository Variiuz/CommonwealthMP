#pragma once

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <WinSock2.h>
#include <WS2tcpip.h>
using CmpSocket = SOCKET;
using CmpSockLen = int;
inline constexpr CmpSocket kCmpInvalidSocket = INVALID_SOCKET;
#else
#include <arpa/inet.h>
#include <cerrno>
#include <fcntl.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>
using CmpSocket = int;
using CmpSockLen = socklen_t;
inline constexpr CmpSocket kCmpInvalidSocket = -1;
#ifndef SOCKET_ERROR
#define SOCKET_ERROR (-1)
#endif
#endif

namespace cmp {

inline bool udp_startup()
{
#ifdef _WIN32
	WSADATA wsa{};
	return WSAStartup(MAKEWORD(2, 2), &wsa) == 0;
#else
	return true;
#endif
}

inline void udp_cleanup()
{
#ifdef _WIN32
	WSACleanup();
#endif
}

inline int udp_last_error()
{
#ifdef _WIN32
	return WSAGetLastError();
#else
	return errno;
#endif
}

inline bool udp_would_block(int err)
{
#ifdef _WIN32
	return err == WSAEWOULDBLOCK || err == WSAETIMEDOUT;
#else
	return err == EWOULDBLOCK || err == EAGAIN || err == EINTR;
#endif
}

inline void udp_close(CmpSocket s)
{
	if (s == kCmpInvalidSocket) {
		return;
	}
#ifdef _WIN32
	closesocket(s);
#else
	close(s);
#endif
}

inline bool udp_set_nonblock(CmpSocket s)
{
#ifdef _WIN32
	u_long n = 1;
	return ioctlsocket(s, FIONBIO, &n) == 0;
#else
	const int flags = fcntl(s, F_GETFL, 0);
	if (flags < 0) {
		return false;
	}
	return fcntl(s, F_SETFL, flags | O_NONBLOCK) == 0;
#endif
}

inline bool udp_set_recv_timeout_ms(CmpSocket s, int ms)
{
#ifdef _WIN32
	DWORD t = static_cast<DWORD>(ms);
	return setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&t), sizeof(t)) == 0;
#else
	timeval tv{};
	tv.tv_sec = ms / 1000;
	tv.tv_usec = (ms % 1000) * 1000;
	return setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv)) == 0;
#endif
}

inline int udp_poll_in(CmpSocket s, int timeoutMs)
{
#ifdef _WIN32
	WSAPOLLFD pfd{};
	pfd.fd = s;
	pfd.events = POLLIN;
	return WSAPoll(&pfd, 1, timeoutMs);
#else
	pollfd pfd{};
	pfd.fd = s;
	pfd.events = POLLIN;
	return poll(&pfd, 1, timeoutMs);
#endif
}

}  // namespace cmp
