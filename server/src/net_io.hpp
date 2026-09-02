#pragma once

#include <atomic>
#include <cstdint>
#include <thread>
#include <unordered_map>
#include <vector>

#include "cmp_net.hpp"
#include "cmp_poll.hpp"
#include "queues.hpp"

struct NetConn {
	CmpSocket sock{ kCmpInvalidSocket };
	sockaddr_in addr{};
	std::vector<std::uint8_t> recvBuf;
	cmp::TcpSendQueue sendQ;
	std::uint32_t peerId{ 0 };
	double connectedAt{ 0.0 };
	bool wantOut{ false };
};

class NetIo {
public:
	NetIo(CmpSocket udp, CmpSocket tcpListen, AsyncQueue<InboundEvent>& inbound, AsyncQueue<OutboundCmd>& outbound);
	~NetIo();

	NetIo(const NetIo&) = delete;
	NetIo& operator=(const NetIo&) = delete;

	void start();
	void stop();

private:
	void loop();
	void process_outbound();
	void accept_ready();
	void poll_udp();
	void poll_conn(std::uint64_t connId, NetConn& conn, short revents);
	void close_conn(std::uint64_t connId, bool notifySim);
	bool push_inbound(InboundEvent ev);

	CmpSocket udpSock_;
	CmpSocket tcpListen_;
	AsyncQueue<InboundEvent>& inbound_;
	AsyncQueue<OutboundCmd>& outbound_;
	std::unordered_map<std::uint64_t, NetConn> conns_;
	std::unordered_map<std::uint32_t, std::uint64_t> peerToConn_;
	std::uint64_t nextConnId_{ 1 };
	std::atomic<bool> running_{ false };
	std::thread thread_;
};
