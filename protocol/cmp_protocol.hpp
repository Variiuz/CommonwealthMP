#pragma once

#include <cstdint>
#include <cstring>
#include <string>
#include <string_view>

// UDP messages for CommonwealthMP.

namespace cmp {

inline constexpr char kMagic[4] = { 'C', 'M', 'P', '1' };
inline constexpr std::uint8_t kProtocolVersion = 11;
inline constexpr std::uint32_t kPluginVersion = 11;
inline constexpr std::uint16_t kDefaultPort = 7777;
inline constexpr std::uint32_t kFakePeerId = 2;
inline constexpr std::uint32_t kFakePeerBegin = 2;
inline constexpr std::uint32_t kFakePeerEnd = 6;
inline constexpr int kFakeCountMax = 5;
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
	SessionInfo = 10,
	ActorPose = 11,
	Hit = 12,
	Chat = 13,
	Kick = 14,
	Heartbeat = 15,
	Teleport = 16,
	SessionRules = 17,
	Ack = 18,
	NackChunk = 19
};

enum class RejectReason : std::uint32_t {
	None = 0,
	Protocol = 1,
	PluginVersion = 2,
	NotInWorld = 4,
	NoHost = 8,
	HostNotStreaming = 16,
	Full = 32,
	Banned = 64,
	Password = 128,
	ModMismatch = 256
};

inline constexpr std::uint8_t kHelloFlagRequireHost = 1u << 0;

inline constexpr std::uint32_t kSessionPvpEnabled = 1u << 0;
inline constexpr std::uint32_t kSessionPasswordRequired = 1u << 1;
inline constexpr float kGuestSpawnOffsetX = 80.0f;

#pragma pack(push, 1)
namespace HeaderFlag {
	inline constexpr std::uint8_t Reliable = 1u << 0;
}

struct Header {
	char magic[4];
	std::uint8_t type;
	std::uint8_t version;
	std::uint16_t size;
	std::uint16_t seq;
	std::uint8_t flags;
	std::uint8_t reserved;
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
	std::uint32_t modHash;
	char password[16];
};

struct Welcome {
	Header header;
	std::uint32_t peerId;
	std::uint32_t fakePeerId;
	std::uint8_t isNewPlayer;
	std::uint8_t isHost;
	std::uint32_t sessionFlags;
};

struct SessionRules {
	Header header;
	std::uint32_t sessionFlags;
};

namespace PoseFlag {
	inline constexpr std::uint32_t Sneak = 1u << 0;
	inline constexpr std::uint32_t Sprint = 1u << 1;
	inline constexpr std::uint32_t Drawn = 1u << 2;
	inline constexpr std::uint32_t Reloading = 1u << 3;
	inline constexpr std::uint32_t Attacking = 1u << 4;
	inline constexpr std::uint32_t Jumping = 1u << 5;
	inline constexpr std::uint32_t Sighted = 1u << 6;
	inline constexpr std::uint32_t Dead = 1u << 7;
	inline constexpr std::uint32_t Pipboy = 1u << 8;
	inline constexpr std::uint32_t Menu = 1u << 9;
	inline constexpr std::uint32_t SlowWalk = 1u << 10;
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

struct Hit {
	Header header;
	std::uint32_t attackerPeerId;
	std::uint32_t targetPeerId;
	float damage;
	std::uint32_t flags;
};

struct Chat {
	Header header;
	std::uint32_t fromPeerId;
	char text[80];
};

struct Kick {
	Header header;
	std::uint32_t targetPeerId;
	char reason[80];
};

struct Heartbeat {
	Header header;
	std::uint32_t peerId;
};

struct Teleport {
	Header header;
	std::uint32_t targetPeerId;
	float x;
	float y;
	float z;
	std::uint32_t locationFormId;
};

struct ActorPose {
	Header header;
	std::uint32_t refFormId;
	std::uint32_t baseFormId;
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
	std::uint8_t unique;
	std::uint8_t dead;
	std::uint8_t pad[2];
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
	std::uint32_t sessionFlags;
	char serverName[32];
	std::uint32_t maxPlayers;
	char motd[64];
};

struct Ack {
	Header header;
	std::uint16_t ackSeq;
	std::uint16_t pad;
	std::uint32_t peerId;
};

struct NackChunk {
	Header header;
	std::uint32_t peerId;
	std::uint8_t blobType;
	std::uint8_t pad;
	std::uint16_t chunkIndex;
	std::uint16_t chunkCount;
	std::uint16_t blobBytes;
};
#pragma pack(pop)

using AppearanceChunk = BlobChunk;
using InventoryChunk = BlobChunk;

static_assert(sizeof(Header) == 12);
static_assert(sizeof(Hello) == 135);
static_assert(sizeof(Welcome) == 26);
static_assert(sizeof(SessionRules) == 16);
static_assert(sizeof(PlayerPose) == 56);
static_assert(sizeof(Bye) == 16);
static_assert(sizeof(Hit) == 28);
static_assert(sizeof(Chat) == 96);
static_assert(sizeof(Kick) == 96);
static_assert(sizeof(Heartbeat) == 16);
static_assert(sizeof(Teleport) == 32);
static_assert(sizeof(ActorPose) == 64);
static_assert(sizeof(Reject) == 112);
static_assert(sizeof(WorldSnapshot) == 64);
static_assert(sizeof(BlobChunk) == 24);
static_assert(sizeof(SessionQuery) == 20);
static_assert(sizeof(SessionInfo) == 142);
static_assert(sizeof(Ack) == 20);
static_assert(sizeof(NackChunk) == 24);

inline constexpr std::size_t kBlobPayloadMax = kMaxDatagram - sizeof(BlobChunk);

template <class T>
void fill_header(T& msg, Msg type)
{
	std::memcpy(msg.header.magic, kMagic, 4);
	msg.header.type = static_cast<std::uint8_t>(type);
	msg.header.version = kProtocolVersion;
	msg.header.size = static_cast<std::uint16_t>(sizeof(T));
	msg.header.seq = 0;
	msg.header.flags = 0;
	msg.header.reserved = 0;
}

inline bool msg_is_reliable(Msg type)
{
	switch (type) {
	case Msg::Welcome:
	case Msg::Reject:
	case Msg::WorldSnapshot:
	case Msg::AppearanceChunk:
	case Msg::InventoryChunk:
	case Msg::Chat:
	case Msg::Kick:
	case Msg::Teleport:
	case Msg::SessionRules:
	case Msg::Bye:
	case Msg::NackChunk:
	case Msg::Hit:
		return true;
	default:
		return false;
	}
}

inline bool expected_msg_size(Msg type, std::uint16_t size)
{
	switch (type) {
	case Msg::Hello:
		return size == sizeof(Hello);
	case Msg::Welcome:
		return size == sizeof(Welcome);
	case Msg::PlayerPose:
		return size == sizeof(PlayerPose);
	case Msg::Bye:
		return size == sizeof(Bye);
	case Msg::Reject:
		return size == sizeof(Reject);
	case Msg::WorldSnapshot:
		return size == sizeof(WorldSnapshot);
	case Msg::AppearanceChunk:
	case Msg::InventoryChunk:
		return size >= sizeof(BlobChunk);
	case Msg::SessionQuery:
		return size == sizeof(SessionQuery);
	case Msg::SessionInfo:
		return size == sizeof(SessionInfo);
	case Msg::ActorPose:
		return size == sizeof(ActorPose);
	case Msg::Hit:
		return size == sizeof(Hit);
	case Msg::Chat:
		return size == sizeof(Chat);
	case Msg::Kick:
		return size == sizeof(Kick);
	case Msg::Heartbeat:
		return size == sizeof(Heartbeat);
	case Msg::Teleport:
		return size == sizeof(Teleport);
	case Msg::SessionRules:
		return size == sizeof(SessionRules);
	case Msg::Ack:
		return size == sizeof(Ack);
	case Msg::NackChunk:
		return size == sizeof(NackChunk);
	default:
		return false;
	}
}

inline bool header_ok(const Header& h, std::size_t nbytes)
{
	if (std::memcmp(h.magic, kMagic, 4) != 0 || h.size > nbytes || h.size < sizeof(Header)) {
		return false;
	}
	const auto type = static_cast<Msg>(h.type);
	if (!expected_msg_size(type, h.size)) {
		return false;
	}
	return true;
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

inline bool is_fake_peer(std::uint32_t peerId)
{
	return peerId >= kFakePeerBegin && peerId <= kFakePeerEnd;
}

inline int clamp_fake_count(int n)
{
	if (n < 1) {
		return 1;
	}
	if (n > kFakeCountMax) {
		return kFakeCountMax;
	}
	return n;
}

inline float clamp_hit_damage(float damage)
{
	if (!(damage > 0.0f)) {
		return 0.0f;
	}
	if (damage > 500.0f) {
		return 500.0f;
	}
	return damage;
}

inline constexpr int kFakeAnimStepCount = 8;
inline constexpr int kFakeAnimStepTicks = 40; // 2s at 20 Hz

inline std::uint32_t fake_anim_flags(int tick, int dummyIndex = 0)
{
	static constexpr std::uint32_t kSteps[] = {
		PoseFlag::Drawn,
		PoseFlag::Drawn | PoseFlag::Sighted,
		PoseFlag::Drawn | PoseFlag::Sighted | PoseFlag::Attacking,
		PoseFlag::Drawn | PoseFlag::Reloading,
		PoseFlag::Jumping,
		PoseFlag::Sneak,
		PoseFlag::Sprint,
		0,
	};
	static_assert(static_cast<int>(sizeof(kSteps) / sizeof(kSteps[0])) == kFakeAnimStepCount);
	if (tick < 0) {
		tick = 0;
	}
	if (dummyIndex < 0) {
		dummyIndex = 0;
	}
	const int step = ((tick / kFakeAnimStepTicks) + dummyIndex) % kFakeAnimStepCount;
	return kSteps[step];
}

inline bool fake_anim_holds_still(std::uint32_t flags)
{
	return has_pose_flag(flags, PoseFlag::Drawn) || has_pose_flag(flags, PoseFlag::Jumping);
}

inline const char* fake_anim_name(std::uint32_t flags)
{
	if (has_pose_flag(flags, PoseFlag::Attacking)) {
		return "fire";
	}
	if (has_pose_flag(flags, PoseFlag::Reloading)) {
		return "reload";
	}
	if (has_pose_flag(flags, PoseFlag::Sighted)) {
		return "ads";
	}
	if (has_pose_flag(flags, PoseFlag::Pipboy)) {
		return "pipboy";
	}
	if (has_pose_flag(flags, PoseFlag::Menu)) {
		return "menu";
	}
	if (has_pose_flag(flags, PoseFlag::Jumping)) {
		return "jump";
	}
	if (has_pose_flag(flags, PoseFlag::Drawn)) {
		return "draw";
	}
	if (has_pose_flag(flags, PoseFlag::SlowWalk)) {
		return "slowwalk";
	}
	if (has_pose_flag(flags, PoseFlag::Sneak)) {
		return "sneak";
	}
	if (has_pose_flag(flags, PoseFlag::Sprint)) {
		return "sprint";
	}
	return "walk";
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
	std::uint8_t flags = 0,
	std::uint32_t modHash = 0,
	std::string_view password = {})
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
	msg.modHash = modHash;
	copy_cstr(msg.password, sizeof(msg.password), password);
	return msg;
}

inline Welcome make_welcome(
	std::uint32_t peerId,
	std::uint32_t fakePeerId,
	bool isNewPlayer,
	bool isHost,
	std::uint32_t sessionFlags = 0)
{
	Welcome msg{};
	fill_header(msg, Msg::Welcome);
	msg.peerId = peerId;
	msg.fakePeerId = fakePeerId;
	msg.isNewPlayer = isNewPlayer ? 1 : 0;
	msg.isHost = isHost ? 1 : 0;
	msg.sessionFlags = sessionFlags;
	return msg;
}

inline SessionRules make_session_rules(std::uint32_t sessionFlags)
{
	SessionRules msg{};
	fill_header(msg, Msg::SessionRules);
	msg.sessionFlags = sessionFlags;
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

inline ActorPose make_actor_pose(
	std::uint32_t refFormId,
	std::uint32_t baseFormId,
	std::uint32_t locationFormId,
	float x,
	float y,
	float z,
	float yaw,
	float pitch = 0.0f,
	float speed = 0.0f,
	float vx = 0.0f,
	float vy = 0.0f,
	std::uint32_t flags = 0,
	bool unique = true,
	bool dead = false)
{
	ActorPose msg{};
	fill_header(msg, Msg::ActorPose);
	msg.refFormId = refFormId;
	msg.baseFormId = baseFormId;
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
	msg.unique = unique ? 1 : 0;
	msg.dead = dead ? 1 : 0;
	return msg;
}

inline Bye make_bye(std::uint32_t peerId)
{
	Bye msg{};
	fill_header(msg, Msg::Bye);
	msg.peerId = peerId;
	return msg;
}

inline Hit make_hit(std::uint32_t attackerPeerId, std::uint32_t targetPeerId, float damage, std::uint32_t flags = 0)
{
	Hit msg{};
	fill_header(msg, Msg::Hit);
	msg.attackerPeerId = attackerPeerId;
	msg.targetPeerId = targetPeerId;
	msg.damage = clamp_hit_damage(damage);
	msg.flags = flags;
	return msg;
}

inline Chat make_chat(std::uint32_t fromPeerId, std::string_view text)
{
	Chat msg{};
	fill_header(msg, Msg::Chat);
	msg.fromPeerId = fromPeerId;
	copy_cstr(msg.text, sizeof(msg.text), text);
	return msg;
}

inline Kick make_kick(std::uint32_t targetPeerId, std::string_view reason)
{
	Kick msg{};
	fill_header(msg, Msg::Kick);
	msg.targetPeerId = targetPeerId;
	copy_cstr(msg.reason, sizeof(msg.reason), reason);
	return msg;
}

inline Heartbeat make_heartbeat(std::uint32_t peerId)
{
	Heartbeat msg{};
	fill_header(msg, Msg::Heartbeat);
	msg.peerId = peerId;
	return msg;
}

inline Teleport make_teleport(std::uint32_t targetPeerId, float x, float y, float z, std::uint32_t locationFormId)
{
	Teleport msg{};
	fill_header(msg, Msg::Teleport);
	msg.targetPeerId = targetPeerId;
	msg.x = x;
	msg.y = y;
	msg.z = z;
	msg.locationFormId = locationFormId;
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
	std::string_view motd = {},
	std::uint32_t sessionFlags = 0)
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
	msg.sessionFlags = sessionFlags;
	copy_cstr(msg.serverName, sizeof(msg.serverName), serverName);
	msg.maxPlayers = maxPlayers;
	copy_cstr(msg.motd, sizeof(msg.motd), motd);
	return msg;
}

inline Ack make_ack(std::uint16_t ackSeq, std::uint32_t peerId = 0)
{
	Ack msg{};
	fill_header(msg, Msg::Ack);
	msg.ackSeq = ackSeq;
	msg.peerId = peerId;
	return msg;
}

inline NackChunk make_nack_chunk(
	std::uint32_t peerId,
	Msg blobType,
	std::uint16_t chunkIndex,
	std::uint16_t chunkCount,
	std::uint16_t blobBytes)
{
	NackChunk msg{};
	fill_header(msg, Msg::NackChunk);
	msg.peerId = peerId;
	msg.blobType = static_cast<std::uint8_t>(blobType);
	msg.chunkIndex = chunkIndex;
	msg.chunkCount = chunkCount;
	msg.blobBytes = blobBytes;
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
	case RejectReason::Banned:
		return "banned";
	case RejectReason::Password:
		return "password";
	case RejectReason::ModMismatch:
		return "mod mismatch";
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
	case Msg::ActorPose:
		return "ActorPose";
	case Msg::Hit:
		return "Hit";
	case Msg::Chat:
		return "Chat";
	case Msg::Kick:
		return "Kick";
	case Msg::Heartbeat:
		return "Heartbeat";
	case Msg::Teleport:
		return "Teleport";
	case Msg::SessionRules:
		return "SessionRules";
	case Msg::Ack:
		return "Ack";
	case Msg::NackChunk:
		return "NackChunk";
	}
	return "Unknown";
}

}  // namespace cmp
