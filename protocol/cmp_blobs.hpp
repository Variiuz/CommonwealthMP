#pragma once

#include "cmp_protocol.hpp"

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace cmp {

inline constexpr std::size_t kPluginField = 24;
inline constexpr std::uint32_t kInvMagic = 0x54564E49;
inline constexpr std::uint16_t kInvVersion = 1;
inline constexpr int kMaxWorn = 32;
inline constexpr int kMaxStacks = 192;
inline constexpr std::uint16_t kMaxBlobChunks = static_cast<std::uint16_t>(
	(0xFFFFu + kBlobPayloadMax - 1) / kBlobPayloadMax);

struct PackedForm {
	std::uint32_t raw{ 0 };
	char plugin[kPluginField]{};
};

inline PackedForm pack_form_id(std::uint32_t formId, std::string_view plugin, bool isLight)
{
	PackedForm p;
	p.raw = isLight ? formId : (formId & 0x00FFFFFF);
	copy_cstr(p.plugin, sizeof(p.plugin), plugin);
	return p;
}

inline std::uint32_t full_form_id(std::uint32_t raw, std::uint8_t compileIndex, bool isLight)
{
	if (!raw) {
		return 0;
	}
	if (isLight) {
		return raw;
	}
	return (static_cast<std::uint32_t>(compileIndex) << 24) | (raw & 0x00FFFFFF);
}

struct BlobWriter {
	std::vector<std::uint8_t> bytes;

	void raw(const void* p, std::size_t n)
	{
		const auto* b = static_cast<const std::uint8_t*>(p);
		bytes.insert(bytes.end(), b, b + n);
	}

	void u8(std::uint8_t v) { bytes.push_back(v); }
	void u16(std::uint16_t v) { raw(&v, 2); }
	void u32(std::uint32_t v) { raw(&v, 4); }
	void f32(float v) { raw(&v, 4); }

	void plugin(const char* name)
	{
		char buf[kPluginField]{};
		if (name) {
			const auto n = std::min(std::strlen(name), kPluginField - 1);
			std::memcpy(buf, name, n);
		}
		raw(buf, kPluginField);
	}

	void write_form(const PackedForm& form)
	{
		u32(form.raw);
		plugin(form.plugin);
	}
};

struct BlobReader {
	const std::uint8_t* p{ nullptr };
	const std::uint8_t* end{ nullptr };

	explicit BlobReader(std::span<const std::uint8_t> s) :
		p(s.data()), end(s.data() + s.size()) {}

	bool need(std::size_t n) const { return p && static_cast<std::size_t>(end - p) >= n; }

	bool raw(void* out, std::size_t n)
	{
		if (!need(n)) {
			return false;
		}
		std::memcpy(out, p, n);
		p += n;
		return true;
	}

	bool u8(std::uint8_t& v) { return raw(&v, 1); }
	bool u16(std::uint16_t& v) { return raw(&v, 2); }
	bool u32(std::uint32_t& v) { return raw(&v, 4); }
	bool f32(float& v) { return raw(&v, 4); }

	bool plugin(char out[kPluginField])
	{
		if (!raw(out, kPluginField)) {
			return false;
		}
		out[kPluginField - 1] = '\0';
		return true;
	}

	bool read_form(PackedForm& form)
	{
		if (!u32(form.raw) || !plugin(form.plugin)) {
			return false;
		}
		return true;
	}
};

struct InvStack {
	PackedForm form;
	std::uint32_t count{ 0 };
};

struct InventorySheet {
	char name[32]{};
	std::uint8_t sex{ 0 };
	PackedForm race;
	std::vector<PackedForm> worn;
	std::vector<InvStack> stacks;
};

inline bool encode_inventory_sheet(const InventorySheet& sheet, std::vector<std::uint8_t>& out)
{
	BlobWriter w;
	w.u32(kInvMagic);
	w.u16(kInvVersion);
	char name[32]{};
	copy_cstr(name, sizeof(name), sheet.name);
	w.raw(name, sizeof(name));
	w.u8(sheet.sex);
	w.write_form(sheet.race);

	const int nWorn = static_cast<int>(std::min<std::size_t>(sheet.worn.size(), static_cast<std::size_t>(kMaxWorn)));
	w.u8(static_cast<std::uint8_t>(nWorn));
	for (int i = 0; i < nWorn; ++i) {
		w.write_form(sheet.worn[static_cast<std::size_t>(i)]);
	}

	const auto nStacks = static_cast<std::uint16_t>(
		std::min<std::size_t>(sheet.stacks.size(), static_cast<std::size_t>(kMaxStacks)));
	w.u16(nStacks);
	for (std::uint16_t i = 0; i < nStacks; ++i) {
		w.write_form(sheet.stacks[i].form);
		w.u32(sheet.stacks[i].count);
	}

	out = std::move(w.bytes);
	return !out.empty();
}

inline bool decode_inventory_sheet(std::span<const std::uint8_t> bytes, InventorySheet& out)
{
	out = {};
	if (bytes.size() < 8) {
		return false;
	}
	BlobReader r{ bytes };
	std::uint32_t magic = 0;
	std::uint16_t ver = 0;
	if (!r.u32(magic) || magic != kInvMagic || !r.u16(ver)) {
		return false;
	}
	if (!r.raw(out.name, sizeof(out.name))) {
		return false;
	}
	out.name[31] = '\0';
	if (!r.u8(out.sex) || !r.read_form(out.race)) {
		return false;
	}

	std::uint8_t nWorn = 0;
	if (!r.u8(nWorn)) {
		return false;
	}
	if (nWorn > kMaxWorn) {
		return false;
	}
	for (std::uint8_t i = 0; i < nWorn; ++i) {
		PackedForm form{};
		if (!r.read_form(form)) {
			return false;
		}
		if (!form.raw || forbidden_actor_base(form.raw)) {
			continue;
		}
		out.worn.push_back(form);
	}

	std::uint16_t nStacks = 0;
	if (!r.u16(nStacks)) {
		return false;
	}
	if (nStacks > kMaxStacks) {
		return false;
	}
	for (std::uint16_t i = 0; i < nStacks; ++i) {
		InvStack stack{};
		if (!r.read_form(stack.form) || !r.u32(stack.count)) {
			return false;
		}
		if (!stack.form.raw || forbidden_actor_base(stack.form.raw)) {
			continue;
		}
		out.stacks.push_back(stack);
	}
	return true;
}

enum class AssembleStatus {
	Pending,
	Complete,
	Reject
};

struct BlobAssembly {
	std::uint16_t chunkCount{ 0 };
	std::uint16_t blobBytes{ 0 };
	std::vector<std::vector<std::uint8_t>> chunks;
};

inline bool split_blob_chunks(
	Msg type,
	std::uint32_t peerId,
	std::span<const std::uint8_t> blob,
	std::vector<std::vector<std::uint8_t>>& out)
{
	out.clear();
	if (blob.empty() || blob.size() > 0xFFFF) {
		return false;
	}
	const auto total = static_cast<std::uint16_t>(blob.size());
	const auto count = static_cast<std::uint16_t>((blob.size() + kBlobPayloadMax - 1) / kBlobPayloadMax);
	for (std::uint16_t i = 0; i < count; ++i) {
		const std::size_t off = static_cast<std::size_t>(i) * kBlobPayloadMax;
		const auto n = static_cast<std::uint16_t>(std::min(kBlobPayloadMax, blob.size() - off));
		std::vector<std::uint8_t> pkt(sizeof(BlobChunk) + n);
		BlobChunk chunk{};
		fill_header(chunk, type);
		chunk.header.size = static_cast<std::uint16_t>(sizeof(chunk) + n);
		chunk.peerId = peerId;
		chunk.chunkIndex = i;
		chunk.chunkCount = count;
		chunk.blobBytes = total;
		chunk.payloadBytes = n;
		std::memcpy(pkt.data(), &chunk, sizeof(chunk));
		std::memcpy(pkt.data() + sizeof(chunk), blob.data() + off, n);
		out.push_back(std::move(pkt));
	}
	return true;
}

inline AssembleStatus assemble_blob_chunk(
	BlobAssembly& a,
	std::span<const std::uint8_t> datagram,
	std::vector<std::uint8_t>& out)
{
	if (datagram.size() < sizeof(BlobChunk)) {
		return AssembleStatus::Reject;
	}
	BlobChunk chunk{};
	std::memcpy(&chunk, datagram.data(), sizeof(chunk));
	if (chunk.payloadBytes == 0 || chunk.payloadBytes > kBlobPayloadMax
		|| sizeof(chunk) + chunk.payloadBytes > datagram.size()) {
		return AssembleStatus::Reject;
	}
	if (chunk.chunkCount == 0 || chunk.chunkCount > kMaxBlobChunks || chunk.chunkIndex >= chunk.chunkCount) {
		return AssembleStatus::Reject;
	}
	if (a.chunkCount != chunk.chunkCount || a.blobBytes != chunk.blobBytes) {
		a.chunkCount = chunk.chunkCount;
		a.blobBytes = chunk.blobBytes;
		a.chunks.assign(chunk.chunkCount, {});
	}
	a.chunks[chunk.chunkIndex] = std::vector<std::uint8_t>(
		datagram.data() + sizeof(chunk),
		datagram.data() + sizeof(chunk) + chunk.payloadBytes);

	std::size_t got = 0;
	for (const auto& c : a.chunks) {
		if (c.empty()) {
			return AssembleStatus::Pending;
		}
		got += c.size();
	}
	if (got != chunk.blobBytes) {
		return AssembleStatus::Reject;
	}
	out.clear();
	out.reserve(got);
	for (const auto& c : a.chunks) {
		out.insert(out.end(), c.begin(), c.end());
	}
	a = {};
	return AssembleStatus::Complete;
}

}  // namespace cmp
