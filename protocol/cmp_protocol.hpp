#pragma once

#include <cstdint>
#include <cstring>
#include <string>
#include <string_view>

// UDP messages for CommonwealthMP.

namespace cmp {

inline constexpr char kMagic[4] = { 'C', 'M', 'P', '1' };
inline constexpr std::uint8_t kProtocolVersion = 6;
inline constexpr std::uint32_t kPluginVersion = 6;
inline constexpr std::uint16_t kDefaultPort = 7777;
inline constexpr std::uint32_t kFakePeerId = 2;
inline constexpr std::uint32_t kCommonwealthWorldspace = 0x0000003C;
inline constexpr std::size_t kMaxDatagram = 512;

inline constexpr float kSanctuaryX = -79348.0f;
inline constexpr float kSanctuaryY = 89752.0f;
inline constexpr float kSanctuaryZ = 7936.0f;

inline constexpr std::uint32_t kPlayerBaseForm = 0x00000007;
inline constexpr std::uint32_t kEncWorkshopNpcForm = 0x00020593;

enum class Msg : std::uint8_t {
	Hello = 1,
	Welcome = 2,
	PlayerPose = 3,
	Bye = 4,
	Reject = 5,
	WorldSnapshot = 6,
	AppearanceChunk = 7,
	InventoryChunk = 8,
	SessionQuery = 9,
	SessionInfo = 10
};

enum class RejectReason : std::uint32_t {
	None = 0,
	Protocol = 1,
	PluginVersion = 2,
	NotInWorld = 4,
	NoHost = 8,
	HostNotStreaming = 16,
	Full = 32
};

inline constexpr std::uint8_t kHelloFlagRequireHost = 1u << 0;
inline constexpr float kGuestSpawnOffsetX = 80.0f;

#pragma pack(push, 1)
struct Header {
	char magic[4];
	std::uint8_t type;
	std::uint8_t version;
	std::uint16_t size;
};

struct Hello {
	Header header;
	std::uint32_t protocol;
	std::uint32_t pluginVersion;
	char playerKey[32];
	char name[32];
	std::uint32_t locationFormId;
	float gameDaysPassed;
	float gameHour;
	std::uint32_t weatherFormId;
	float x;
	float y;
	float z;
	std::uint8_t inWorld;
	std::uint8_t interior;
	std::uint8_t flags;
	std::uint8_t pad;
};

struct Welcome {
	Header header;
	std::uint32_t peerId;
	std::uint32_t fakePeerId;
	std::uint8_t isNewPlayer;
	std::uint8_t isHost;
	std::uint8_t pad[2];
};

namespace PoseFlag {
	inline constexpr std::uint32_t Sneak = 1u << 0;
	inline constexpr std::uint32_t Sprint = 1u << 1;
	inline constexpr std::uint32_t Drawn = 1u << 2;
	inline constexpr std::uint32_t Reloading = 1u << 3;
	inline constexpr std::uint32_t Attacking = 1u << 4;
	inline constexpr std::uint32_t Jumping = 1u << 5;
	inline constexpr std::uint32_t Sighted = 1u << 6;
}

inline bool has_pose_flag(std::uint32_t flags, std::uint32_t bit)
{
	return (flags & bit) != 0;
}

struct PlayerPose {
	Header header;
	std::uint32_t peerId;
	std::uint32_t locationFormId;
	float x;
	float y;
	float z;
	float yaw;
	float pitch;
	float speed;
	float vx;
	float vy;
	std::uint32_t flags;
};

struct Bye {
	Header header;
	std::uint32_t peerId;
};

struct Reject {
	Header header;
	std::uint32_t reason;
	char message[96];
};

struct WorldSnapshot {
	Header header;
	float gameHour;
	float gameDaysPassed;
	std::uint32_t weatherFormId;
	std::uint32_t hostLocationFormId;
	float hostX;
	float hostY;
	float hostZ;
	std::uint32_t hostPeerId;
	std::uint32_t placeLocationFormId;
	float placeX;
	float placeY;
	float placeZ;
	std::uint8_t isNewPlayer;
	std::uint8_t pad[3];
};

struct BlobChunk {
	Header header;
	std::uint32_t peerId;
	std::uint16_t chunkIndex;
	std::uint16_t chunkCount;
	std::uint16_t blobBytes;
	std::uint16_t payloadBytes;
};

struct SessionQuery {
	Header header;
	std::uint32_t pluginVersion;
	std::uint8_t pad[4];
};

struct SessionInfo {
	Header header;
	std::uint32_t hostPeerId;
	std::uint32_t hostLocationFormId;
	float hostX;
	float hostY;
	float hostZ;
	std::uint32_t clientCount;
	std::uint8_t haveHost;
	std::uint8_t hostInterior;
	std::uint8_t pad[2];
	char serverName[32];
	std::uint32_t maxPlayers;
	char motd[64];
};
#pragma pack(pop)

using AppearanceChunk = BlobChunk;
using InventoryChunk = BlobChunk;

static_assert(sizeof(Header) == 8);
static_assert(sizeof(Hello) == 112);
static_assert(sizeof(Welcome) == 20);
static_assert(sizeof(PlayerPose) == 52);
static_assert(sizeof(Bye) == 12);
static_assert(sizeof(Reject) == 108);
static_assert(sizeof(WorldSnapshot) == 60);
static_assert(sizeof(BlobChunk) == 20);
static_assert(sizeof(SessionQuery) == 16);
static_assert(sizeof(SessionInfo) == 136);

inline constexpr std::size_t kBlobPayloadMax = kMaxDatagram - sizeof(BlobChunk);

inline bool header_ok(const Header& h, std::size_t nbytes)
{
	return std::memcmp(h.magic, kMagic, 4) == 0 && h.size <= nbytes && h.size >= sizeof(Header);
}

template <class T>
void fill_header(T& msg, Msg type)
{
	std::memcpy(msg.header.magic, kMagic, 4);
	msg.header.type = static_cast<std::uint8_t>(type);
	msg.header.version = kProtocolVersion;
	msg.header.size = static_cast<std::uint16_t>(sizeof(T));
}

inline void copy_cstr(char* dest, std::size_t destSize, std::string_view src)
{
	if (!dest || destSize == 0) {
		return;
	}
	const auto n = src.size() < destSize - 1 ? src.size() : destSize - 1;
	std::memcpy(dest, src.data(), n);
	dest[n] = '\0';
}

inline bool forbidden_actor_base(std::uint32_t formId)
{
	const auto local = formId & 0x00FFFFFF;
	return local == (kPlayerBaseForm & 0x00FFFFFF) || local == (kEncWorkshopNpcForm & 0x00FFFFFF);
}

inline Hello make_hello(
	std::string_view name,
	std::string_view playerKey,
	bool inWorld,
	std::uint32_t locationFormId,
	float gameDaysPassed,
	float gameHour,
	std::uint32_t weatherFormId,
	float x,
	float y,
	float z,
	bool interior,
	std::uint8_t flags = 0)
{
	Hello msg{};
	fill_header(msg, Msg::Hello);
	msg.protocol = kProtocolVersion;
	msg.pluginVersion = kPluginVersion;
	copy_cstr(msg.playerKey, sizeof(msg.playerKey), playerKey);
	copy_cstr(msg.name, sizeof(msg.name), name);
	msg.locationFormId = locationFormId;
	msg.gameDaysPassed = gameDaysPassed;
	msg.gameHour = gameHour;
	msg.weatherFormId = weatherFormId;
	msg.x = x;
	msg.y = y;
	msg.z = z;
	msg.inWorld = inWorld ? 1 : 0;
	msg.interior = interior ? 1 : 0;
	msg.flags = flags;
	return msg;
}

inline Welcome make_welcome(std::uint32_t peerId, std::uint32_t fakePeerId, bool isNewPlayer, bool isHost)
{
	Welcome msg{};
	fill_header(msg, Msg::Welcome);
	msg.peerId = peerId;
	msg.fakePeerId = fakePeerId;
	msg.isNewPlayer = isNewPlayer ? 1 : 0;
	msg.isHost = isHost ? 1 : 0;
	return msg;
}

inline PlayerPose make_pose(
	std::uint32_t peerId,
	std::uint32_t locationFormId,
	float x,
	float y,
	float z,
	float yaw,
	float pitch = 0.0f,
	float speed = 0.0f,
	float vx = 0.0f,
	float vy = 0.0f,
	std::uint32_t flags = 0)
{
	PlayerPose msg{};
	fill_header(msg, Msg::PlayerPose);
	msg.peerId = peerId;
	msg.locationFormId = locationFormId;
	msg.x = x;
	msg.y = y;
	msg.z = z;
	msg.yaw = yaw;
	msg.pitch = pitch;
	msg.speed = speed;
	msg.vx = vx;
	msg.vy = vy;
	msg.flags = flags;
	return msg;
}

inline Bye make_bye(std::uint32_t peerId)
{
	Bye msg{};
	fill_header(msg, Msg::Bye);
	msg.peerId = peerId;
	return msg;
}

inline Reject make_reject(RejectReason reason, std::string_view text)
{
	Reject msg{};
	fill_header(msg, Msg::Reject);
	msg.reason = static_cast<std::uint32_t>(reason);
	copy_cstr(msg.message, sizeof(msg.message), text);
	return msg;
}

inline WorldSnapshot make_world_snapshot(
	float gameHour,
	float gameDaysPassed,
	std::uint32_t weatherFormId,
	std::uint32_t hostLocationFormId,
	float hostX,
	float hostY,
	float hostZ,
	std::uint32_t hostPeerId,
	std::uint32_t placeLocationFormId,
	float placeX,
	float placeY,
	float placeZ,
	bool isNewPlayer)
{
	WorldSnapshot msg{};
	fill_header(msg, Msg::WorldSnapshot);
	msg.gameHour = gameHour;
	msg.gameDaysPassed = gameDaysPassed;
	msg.weatherFormId = weatherFormId;
	msg.hostLocationFormId = hostLocationFormId;
	msg.hostX = hostX;
	msg.hostY = hostY;
	msg.hostZ = hostZ;
	msg.hostPeerId = hostPeerId;
	msg.placeLocationFormId = placeLocationFormId;
	msg.placeX = placeX;
	msg.placeY = placeY;
	msg.placeZ = placeZ;
	msg.isNewPlayer = isNewPlayer ? 1 : 0;
	return msg;
}

inline SessionQuery make_session_query()
{
	SessionQuery msg{};
	fill_header(msg, Msg::SessionQuery);
	msg.pluginVersion = kPluginVersion;
	return msg;
}

inline SessionInfo make_session_info(
	std::uint32_t hostPeerId,
	std::uint32_t hostLocationFormId,
	float hostX,
	float hostY,
	float hostZ,
	std::uint32_t clientCount,
	bool haveHost,
	bool hostInterior,
	std::string_view serverName = {},
	std::uint32_t maxPlayers = 0,
	std::string_view motd = {})
{
	SessionInfo msg{};
	fill_header(msg, Msg::SessionInfo);
	msg.hostPeerId = hostPeerId;
	msg.hostLocationFormId = hostLocationFormId;
	msg.hostX = hostX;
	msg.hostY = hostY;
	msg.hostZ = hostZ;
	msg.clientCount = clientCount;
	msg.haveHost = haveHost ? 1 : 0;
	msg.hostInterior = hostInterior ? 1 : 0;
	copy_cstr(msg.serverName, sizeof(msg.serverName), serverName);
	msg.maxPlayers = maxPlayers;
	copy_cstr(msg.motd, sizeof(msg.motd), motd);
	return msg;
}

inline const char* reject_name(RejectReason reason)
{
	switch (reason) {
	case RejectReason::Protocol:
		return "protocol";
	case RejectReason::PluginVersion:
		return "plugin version";
	case RejectReason::NotInWorld:
		return "not in world";
	case RejectReason::NoHost:
		return "no host";
	case RejectReason::HostNotStreaming:
		return "host not streaming";
	case RejectReason::Full:
		return "server full";
	default:
		return "reject";
	}
}

inline std::string_view msg_name(Msg type)
{
	switch (type) {
	case Msg::Hello:
		return "Hello";
	case Msg::Welcome:
		return "Welcome";
	case Msg::PlayerPose:
		return "PlayerPose";
	case Msg::Bye:
		return "Bye";
	case Msg::Reject:
		return "Reject";
	case Msg::WorldSnapshot:
		return "WorldSnapshot";
	case Msg::AppearanceChunk:
		return "AppearanceChunk";
	case Msg::InventoryChunk:
		return "InventoryChunk";
	case Msg::SessionQuery:
		return "SessionQuery";
	case Msg::SessionInfo:
		return "SessionInfo";
	}
	return "Unknown";
}

}  // namespace cmp
