#include "net_io.hpp"

#include <cstring>
#include <vector>

#include "cmp_udp.hpp"
#include "log.hpp"
#include "server_state.hpp"

NetIo::NetIo(CmpSocket udp, CmpSocket tcpListen, AsyncQueue<InboundEvent>& inbound, AsyncQueue<OutboundCmd>& outbound)
	: udpSock_(udp), tcpListen_(tcpListen), inbound_(inbound), outbound_(outbound)
{
}

NetIo::~NetIo()
{
	stop();
}

void NetIo::start()
{
	if (running_.exchange(true)) {
		return;
	}
	thread_ = std::thread([this] { loop(); });
}

void NetIo::stop()
{
	if (!running_.exchange(false)) {
		return;
	}
	if (thread_.joinable()) {
		thread_.join();
	}
	for (auto& [id, conn] : conns_) {
		(void)id;
		if (conn.sock != kCmpInvalidSocket) {
			cmp::udp_close(conn.sock);
			conn.sock = kCmpInvalidSocket;
		}
	}
	conns_.clear();
	peerToConn_.clear();
}

bool NetIo::push_inbound(InboundEvent ev)
{
	if (!inbound_.push(std::move(ev))) {
		LOG_WARN("inbound queue full, dropping event kind=%u", static_cast<unsigned>(ev.kind));
		return false;
	}
	return true;
}

void NetIo::close_conn(std::uint64_t connId, bool notifySim)
{
	auto it = conns_.find(connId);
	if (it == conns_.end()) {
		return;
	}
	if (it->second.peerId != 0) {
		peerToConn_.erase(it->second.peerId);
	}
	if (notifySim) {
		InboundEvent ev;
		ev.kind = InboundKind::TcpClosed;
		ev.connId = connId;
		ev.peerId = it->second.peerId;
		ev.addr = it->second.addr;
		push_inbound(std::move(ev));
	}
	if (it->second.sock != kCmpInvalidSocket) {
		cmp::udp_close(it->second.sock);
	}
	conns_.erase(it);
}

void NetIo::process_outbound()
{
	OutboundCmd cmd;
	while (outbound_.try_pop(cmd)) {
		switch (cmd.kind) {
		case OutboundKind::TcpSend: {
			NetConn* conn = nullptr;
			std::uint64_t id = cmd.connId;
			if (id != 0) {
				auto it = conns_.find(id);
				if (it != conns_.end()) {
					conn = &it->second;
				}
			} else if (cmd.peerId != 0) {
				auto pit = peerToConn_.find(cmd.peerId);
				if (pit != peerToConn_.end()) {
					id = pit->second;
					auto it = conns_.find(id);
					if (it != conns_.end()) {
						conn = &it->second;
					}
				}
			}
			if (!conn || conn->sock == kCmpInvalidSocket) {
				break;
			}
			if (!conn->sendQ.append(cmd.bytes.data(), static_cast<int>(cmd.bytes.size()))) {
				LOG_WARN("tcp send queue overflow peer=%u conn=%llu", cmd.peerId,
					static_cast<unsigned long long>(id));
			}
			bool wantOut = false;
			bool closed = false;
			conn->sendQ.drain(conn->sock, wantOut, closed);
			conn->wantOut = wantOut;
			if (closed) {
				close_conn(id, true);
			}
			break;
		}
		case OutboundKind::UdpSend: {
			if (udpSock_ == kCmpInvalidSocket || cmd.bytes.empty()) {
				break;
			}
			const int n = sendto(udpSock_, reinterpret_cast<const char*>(cmd.bytes.data()),
				static_cast<int>(cmd.bytes.size()), 0, reinterpret_cast<const sockaddr*>(&cmd.addr), sizeof(cmd.addr));
			if (n != static_cast<int>(cmd.bytes.size())) {
				LOG_WARN("udp send failed bytes=%d/%zu err=%d dest=%s", n, cmd.bytes.size(), cmp::udp_last_error(),
					addr_key(cmd.addr).c_str());
			}
			break;
		}
		case OutboundKind::BindPeer: {
			auto it = conns_.find(cmd.connId);
			if (it == conns_.end()) {
				break;
			}
			if (it->second.peerId != 0 && it->second.peerId != cmd.peerId) {
				peerToConn_.erase(it->second.peerId);
			}
			if (cmd.peerId != 0) {
				auto old = peerToConn_.find(cmd.peerId);
				if (old != peerToConn_.end() && old->second != cmd.connId) {
					close_conn(old->second, false);
				}
				peerToConn_[cmd.peerId] = cmd.connId;
			}
			it->second.peerId = cmd.peerId;
			break;
		}
		case OutboundKind::CloseConn: {
			std::uint64_t id = cmd.connId;
			if (id == 0 && cmd.peerId != 0) {
				auto pit = peerToConn_.find(cmd.peerId);
				if (pit != peerToConn_.end()) {
					id = pit->second;
				}
			}
			if (id != 0) {
				close_conn(id, false);
			}
			break;
		}
		case OutboundKind::KeepPending: {
			auto pit = peerToConn_.find(cmd.peerId);
			if (pit == peerToConn_.end()) {
				break;
			}
			auto it = conns_.find(pit->second);
			if (it == conns_.end()) {
				peerToConn_.erase(pit);
				break;
			}
			peerToConn_.erase(pit);
			it->second.peerId = 0;
			it->second.connectedAt = now_sec();
			break;
		}
		}
	}
}

void NetIo::accept_ready()
{
	for (;;) {
		sockaddr_in from{};
		CmpSockLen fromLen = sizeof(from);
		CmpSocket s = accept(tcpListen_, reinterpret_cast<sockaddr*>(&from), &fromLen);
		if (s == kCmpInvalidSocket) {
			const int err = cmp::udp_last_error();
			if (!cmp::udp_would_block(err)) {
				LOG_WARN("accept err=%d", err);
			}
			break;
		}
		cmp::udp_set_nonblock(s);
		cmp::tcp_set_nodelay(s);
		const auto connId = nextConnId_++;
		NetConn conn;
		conn.sock = s;
		conn.addr = from;
		conn.connectedAt = now_sec();
		conns_.emplace(connId, std::move(conn));
		InboundEvent ev;
		ev.kind = InboundKind::TcpAccept;
		ev.connId = connId;
		ev.addr = from;
		push_inbound(std::move(ev));
		LOG_INFO("tcp accept %s conn=%llu", addr_key(from).c_str(), static_cast<unsigned long long>(connId));
	}
}

void NetIo::poll_udp()
{
	for (;;) {
		char buf[kMaxDatagram]{};
		sockaddr_in from{};
		CmpSockLen fromLen = sizeof(from);
		const int n = recvfrom(udpSock_, buf, sizeof(buf), 0, reinterpret_cast<sockaddr*>(&from), &fromLen);
		if (n == SOCKET_ERROR) {
			const int err = cmp::udp_last_error();
			if (!cmp::udp_would_block(err)) {
				LOG_WARN("recvfrom err=%d", err);
			}
			break;
		}
		if (n <= 0) {
			break;
		}
		InboundEvent ev;
		ev.kind = InboundKind::UdpDatagram;
		ev.addr = from;
		ev.bytes.assign(buf, buf + n);
		if (!inbound_.push(std::move(ev))) {
			LOG_WARN("inbound full, drop udp");
		}
	}
}

void NetIo::poll_conn(std::uint64_t connId, NetConn& conn, short revents)
{
	if (revents & (cmp::kPollErr | cmp::kPollHup)) {
		close_conn(connId, true);
		return;
	}
	if (revents & cmp::kPollOut) {
		bool wantOut = false;
		bool closed = false;
		conn.sendQ.drain(conn.sock, wantOut, closed);
		conn.wantOut = wantOut;
		if (closed) {
			close_conn(connId, true);
			return;
		}
	}
	if (revents & cmp::kPollIn) {
		for (;;) {
			const int n = cmp::tcp_recv_append(conn.sock, conn.recvBuf);
			if (n == 0) {
				close_conn(connId, true);
				return;
			}
			if (n < 0) {
				if (n == -2) {
					close_conn(connId, true);
					return;
				}
				const int err = cmp::udp_last_error();
				if (!cmp::udp_would_block(err)) {
					close_conn(connId, true);
					return;
				}
				break;
			}
			std::vector<std::uint8_t> frame;
			while (cmp::tcp_try_extract_frame(conn.recvBuf, frame)) {
				InboundEvent ev;
				ev.kind = InboundKind::TcpFrame;
				ev.connId = connId;
				ev.peerId = conn.peerId;
				ev.addr = conn.addr;
				ev.bytes = std::move(frame);
				if (!push_inbound(std::move(ev))) {
					close_conn(connId, true);
					return;
				}
			}
		}
	}
}

void NetIo::loop()
{
	LOG_INFO("net thread started");
	while (running_.load(std::memory_order_relaxed)) {
		process_outbound();

		std::vector<cmp::PollFd> fds;
		fds.reserve(2 + conns_.size());
		fds.push_back(cmp::PollFd{ udpSock_, cmp::kPollIn, 0 });
		fds.push_back(cmp::PollFd{ tcpListen_, cmp::kPollIn, 0 });
		std::vector<std::uint64_t> connIds;
		connIds.reserve(conns_.size());
		for (auto& [id, conn] : conns_) {
			short ev = cmp::kPollIn;
			if (conn.wantOut || !conn.sendQ.empty()) {
				ev = static_cast<short>(ev | cmp::kPollOut);
			}
			fds.push_back(cmp::PollFd{ conn.sock, ev, 0 });
			connIds.push_back(id);
		}

		const int ready = cmp::poll_fds(fds.data(), static_cast<int>(fds.size()), 5);
		if (!running_.load(std::memory_order_relaxed)) {
			break;
		}
		if (ready < 0) {
			LOG_WARN("poll err=%d", cmp::udp_last_error());
			continue;
		}
		if (ready == 0) {
			continue;
		}

		if (fds[0].revents & cmp::kPollIn) {
			poll_udp();
		}
		if (fds[1].revents & cmp::kPollIn) {
			accept_ready();
		}
		for (std::size_t i = 0; i < connIds.size(); ++i) {
			const auto connId = connIds[i];
			auto it = conns_.find(connId);
			if (it == conns_.end()) {
				continue;
			}
			const short rev = fds[i + 2].revents;
			if (rev == 0) {
				continue;
			}
			poll_conn(connId, it->second, rev);
		}
	}

	process_outbound();
	LOG_INFO("net thread stopped");
}
