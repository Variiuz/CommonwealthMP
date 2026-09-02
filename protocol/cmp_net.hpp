#pragma once

#include "cmp_protocol.hpp"
#include "cmp_udp.hpp"

#ifndef _WIN32
#include <netinet/tcp.h>
#endif

#include <cstdint>
#include <cstring>
#include <vector>

namespace cmp {

inline bool tcp_set_nodelay(CmpSocket s)
{
	int on = 1;
	return setsockopt(s, IPPROTO_TCP, TCP_NODELAY, reinterpret_cast<const char*>(&on), sizeof(on)) == 0;
}

inline bool tcp_send_all(CmpSocket s, const void* data, int len)
{
	if (s == kCmpInvalidSocket || !data || len <= 0) {
		return false;
	}
	const auto* p = static_cast<const char*>(data);
	int sent = 0;
	while (sent < len) {
		const int n = send(s, p + sent, len - sent, 0);
		if (n <= 0) {
			return false;
		}
		sent += n;
	}
	return true;
}

inline bool tcp_send_msg(CmpSocket s, const void* data, int len)
{
	if (!data || len < static_cast<int>(sizeof(Header))) {
		return false;
	}
	const auto* h = static_cast<const Header*>(data);
	if (h->size == 0 || h->size > kMaxTcpFrame || static_cast<int>(h->size) != len) {
		return false;
	}
	return tcp_send_all(s, data, len);
}

// Appends bytes into buf. Returns true when a complete framed message is available in out.
inline bool tcp_try_extract_frame(std::vector<std::uint8_t>& buf, std::vector<std::uint8_t>& out)
{
	out.clear();
	if (buf.size() < sizeof(Header)) {
		return false;
	}
	Header h{};
	std::memcpy(&h, buf.data(), sizeof(h));
	if (std::memcmp(h.magic, kMagic, 4) != 0 || h.size < sizeof(Header) || h.size > kMaxTcpFrame) {
		buf.clear();
		return false;
	}
	if (buf.size() < h.size) {
		return false;
	}
	out.assign(buf.begin(), buf.begin() + static_cast<std::ptrdiff_t>(h.size));
	buf.erase(buf.begin(), buf.begin() + static_cast<std::ptrdiff_t>(h.size));
	return true;
}

inline int tcp_recv_append(CmpSocket s, std::vector<std::uint8_t>& buf, std::size_t maxBuf = kMaxTcpFrame * 2)
{
	char tmp[4096];
	const int n = recv(s, tmp, sizeof(tmp), 0);
	if (n > 0) {
		if (buf.size() + static_cast<std::size_t>(n) > maxBuf) {
			buf.clear();
			return -2;
		}
		buf.insert(buf.end(), tmp, tmp + n);
	}
	return n;
}

inline std::uint32_t make_udp_token()
{
	static std::uint32_t s = 0xC0FFEE01u;
	s = s * 1664525u + 1013904223u;
	if (s == 0) {
		s = 1;
	}
	return s;
}

inline constexpr std::size_t kTcpSendQueueMax = 1024 * 1024;

// Nonblocking TCP outbound buffer with partial-send cursor.
struct TcpSendQueue {
	std::vector<std::uint8_t> bytes;
	std::size_t offset{ 0 };

	[[nodiscard]] bool empty() const { return offset >= bytes.size(); }
	[[nodiscard]] std::size_t pending() const { return empty() ? 0 : bytes.size() - offset; }

	void clear()
	{
		bytes.clear();
		offset = 0;
	}

	bool append(const void* data, int len)
	{
		if (!data || len <= 0) {
			return false;
		}
		if (pending() + static_cast<std::size_t>(len) > kTcpSendQueueMax) {
			return false;
		}
		if (offset > 0 && offset == bytes.size()) {
			bytes.clear();
			offset = 0;
		} else if (offset > 64 * 1024 && offset * 2 > bytes.size()) {
			bytes.erase(bytes.begin(), bytes.begin() + static_cast<std::ptrdiff_t>(offset));
			offset = 0;
		}
		const auto* p = static_cast<const std::uint8_t*>(data);
		bytes.insert(bytes.end(), p, p + len);
		return true;
	}

	// Returns true if still connected / ok. Sets wantPollOut if more data remains.
	bool drain(CmpSocket s, bool& wantPollOut, bool& closed)
	{
		wantPollOut = false;
		closed = false;
		if (s == kCmpInvalidSocket) {
			return false;
		}
		while (!empty()) {
			const int n = send(s, reinterpret_cast<const char*>(bytes.data() + offset),
				static_cast<int>(bytes.size() - offset), 0);
			if (n > 0) {
				offset += static_cast<std::size_t>(n);
				continue;
			}
			const int err = udp_last_error();
			if (udp_would_block(err)) {
				wantPollOut = true;
				return true;
			}
			closed = true;
			return false;
		}
		if (offset > 0) {
			bytes.clear();
			offset = 0;
		}
		return true;
	}
};

}  // namespace cmp
