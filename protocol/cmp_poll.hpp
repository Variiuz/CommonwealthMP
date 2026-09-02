#pragma once

#include "cmp_udp.hpp"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace cmp {

struct PollFd {
	CmpSocket fd{ kCmpInvalidSocket };
	short events{ 0 };
	short revents{ 0 };
};

inline constexpr short kPollIn = POLLIN;
inline constexpr short kPollOut = POLLOUT;
#ifdef _WIN32
inline constexpr short kPollErr = POLLERR;
inline constexpr short kPollHup = POLLHUP;
#else
inline constexpr short kPollErr = POLLERR;
inline constexpr short kPollHup = POLLHUP;
#endif

// Returns number of ready fds, 0 on timeout, <0 on error.
inline int poll_fds(PollFd* fds, int count, int timeoutMs)
{
	if (!fds || count <= 0) {
		return 0;
	}
#ifdef _WIN32
	std::vector<WSAPOLLFD> raw(static_cast<std::size_t>(count));
	for (int i = 0; i < count; ++i) {
		raw[static_cast<std::size_t>(i)].fd = fds[i].fd;
		raw[static_cast<std::size_t>(i)].events = fds[i].events;
		raw[static_cast<std::size_t>(i)].revents = 0;
	}
	const int n = WSAPoll(raw.data(), static_cast<ULONG>(count), timeoutMs);
	if (n > 0) {
		for (int i = 0; i < count; ++i) {
			fds[i].revents = raw[static_cast<std::size_t>(i)].revents;
		}
	}
	return n;
#else
	std::vector<pollfd> raw(static_cast<std::size_t>(count));
	for (int i = 0; i < count; ++i) {
		raw[static_cast<std::size_t>(i)].fd = fds[i].fd;
		raw[static_cast<std::size_t>(i)].events = fds[i].events;
		raw[static_cast<std::size_t>(i)].revents = 0;
	}
	const int n = ::poll(raw.data(), static_cast<nfds_t>(count), timeoutMs);
	if (n > 0) {
		for (int i = 0; i < count; ++i) {
			fds[i].revents = raw[static_cast<std::size_t>(i)].revents;
		}
	}
	return n;
#endif
}

}  // namespace cmp
