#include "pch.h"
#include "session.h"
#include "net.h"
#include "net/internal.h"
#include "udp_win.h"
#include "presence.h"

namespace {

bool g_queryPending = false;
double g_queryStart = 0.0;
SessionQueryResult g_queryResult;

}  // namespace

void CMP_QueryStart(std::string host, std::uint16_t port)
{
	auto& s = CMP_Session();
	if (s.net.joined) {
		CMP_Leave();
	} else {
		cmp_net_shutdown();
	}
	g_queryPending = false;
	g_queryResult = {};
	s.settings.host = std::move(host);
	s.settings.port = port;
	if (!cmp_net_startup()) {
		g_queryResult.error = "net startup failed";
		g_queryResult.ok = false;
		return;
	}
	if (!cmp_net_tcp_connect(s.settings.host.c_str(), s.settings.port)) {
		g_queryResult.error = "tcp connect failed (bad host?)";
		cmp_net_shutdown();
		return;
	}
	const auto q = cmp::make_session_query();
	if (!cmp_net_tcp_send(&q, static_cast<int>(sizeof(q)))) {
		g_queryResult.error = "send query failed";
		cmp_net_shutdown();
		return;
	}
	g_queryPending = true;
	g_queryStart = cmp_net::NowSec();
	s.lastStatus = "querying " + s.settings.host + ":" + std::to_string(s.settings.port);
	REX::INFO("SessionQuery {}:{}", s.settings.host, s.settings.port);
}

bool CMP_QueryPoll(SessionQueryResult& out)
{
	if (!g_queryPending) {
		if (!g_queryResult.error.empty() || g_queryResult.ok) {
			out = g_queryResult;
			return true;
		}
		out.error = "no query";
		return true;
	}

	for (int i = 0; i < 8; ++i) {
		char buf[512]{};
		const int n = cmp_net_tcp_recv_frame(buf, sizeof(buf));
		if (n < static_cast<int>(sizeof(cmp::Header))) {
			break;
		}
		cmp::Header header{};
		std::memcpy(&header, buf, sizeof(header));
		if (!cmp::header_ok(header, static_cast<std::size_t>(n))) {
			continue;
		}
		if (static_cast<cmp::Msg>(header.type) == cmp::Msg::SessionInfo && n >= static_cast<int>(sizeof(cmp::SessionInfo))) {
			std::memcpy(&g_queryResult.info, buf, sizeof(g_queryResult.info));
			g_queryResult.info.serverName[sizeof(g_queryResult.info.serverName) - 1] = '\0';
			g_queryResult.info.motd[sizeof(g_queryResult.info.motd) - 1] = '\0';
			g_queryPending = false;
			cmp_net_shutdown();
			if (!g_queryResult.info.haveHost) {
				g_queryResult.ok = false;
				g_queryResult.error = "no live host on that server";
			} else if (g_queryResult.info.hostInterior || g_queryResult.info.hostLocationFormId != cmp::kCommonwealthWorldspace) {
				g_queryResult.ok = false;
				g_queryResult.error = "host is not Commonwealth exterior";
			} else {
				g_queryResult.ok = true;
				g_queryResult.error.clear();
			}
			out = g_queryResult;
			auto& sess = CMP_Session();
			sess.presence.serverName = g_queryResult.info.serverName;
			sess.presence.maxPlayers = g_queryResult.info.maxPlayers;
			CMP_Presence_Invalidate();
			REX::INFO("SessionInfo name={} clients={}/{} haveHost={} loc={:X} host=({},{},{}) motd={} ok={}",
				g_queryResult.info.serverName,
				g_queryResult.info.clientCount,
				g_queryResult.info.maxPlayers,
				g_queryResult.info.haveHost,
				g_queryResult.info.hostLocationFormId,
				g_queryResult.info.hostX,
				g_queryResult.info.hostY,
				g_queryResult.info.hostZ,
				g_queryResult.info.motd,
				g_queryResult.ok ? 1 : 0);
			return true;
		}
	}

	if (cmp_net::NowSec() - g_queryStart > 2.0) {
		g_queryPending = false;
		cmp_net_shutdown();
		g_queryResult.ok = false;
		g_queryResult.error = "server timeout (need matching protocol server)";
		out = g_queryResult;
		REX::INFO("SessionQuery timeout");
		return true;
	}
	return false;
}

bool CMP_PlayerInCommonwealth()
{
	const auto world = cmp_net::ReadLocalWorld();
	return world.inWorld && !world.interior && world.location == cmp::kCommonwealthWorldspace;
}

void CMP_EnsureCommonwealthExterior()
{
	if (CMP_PlayerInCommonwealth()) {
		return;
	}
	RE::Console::ExecuteCommand("coc SanctuaryExt");
	REX::INFO("coc SanctuaryExt (menu join needs Commonwealth exterior)");
}
