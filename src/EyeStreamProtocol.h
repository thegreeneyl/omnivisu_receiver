#pragma once

#include <cstdint>

/// Shared wire protocol for the eye video UDP stream. This header MUST stay
/// byte-for-byte identical in the sender (omnivisu) and receiver
/// (omnivisu_receiver) projects.
///
/// Each UDP datagram is one PacketHeader immediately followed by `payloadBytes`
/// of frame data. A full frame is split across `totalPackets` datagrams that
/// share the same `frameId`; the receiver reassembles by frameId and discards
/// any frame it cannot complete. All fields are little-endian (both ends are
/// little-endian x86/arm64, so we send the struct as-is without byte swapping).
///
/// `sessionId` is a random value chosen once per sender run. `frameId` is only
/// monotonic *within* a session and resets to 0 every time the sender restarts,
/// so the receiver MUST key its newest-frame-wins ordering on sessionId and
/// reset all reassembly state whenever the sessionId changes; otherwise a
/// restarted sender's low frameIds look "stale" and get dropped forever.
///
/// Besides the video packets there is a second, independent packet type on the
/// SAME port: the mouth/fade state packet (magic "MOUT", StatePacket below).
/// It is one small datagram (no fragmentation, no reassembly), sent every
/// sender frame - including while the video stream is gated off at fade 0 -
/// and consumed latest-wins keyed on (sessionId, sequence). Fire-and-forget:
/// if either end disappears the other just keeps its last state and picks up
/// again as soon as datagrams flow.
namespace eyestream {

constexpr std::uint32_t kMagic = 0x45594553; // "EYES"
constexpr std::uint8_t kFormatRawRgb = 0;
constexpr std::uint8_t kFormatJpeg = 1;

/// Magic of the mouth/fade state datagram ("MOUT").
constexpr std::uint32_t kStateMagic = 0x4D4F5554;
/// Sanity cap on lightsW * lightsH so a corrupt packet can't ask for huge
/// buffers (the real grid is 14x1 today).
constexpr int kMaxStateLights = 64;

#pragma pack(push, 1)
struct PacketHeader {
	std::uint32_t magic = kMagic;
	std::uint32_t sessionId = 0; ///< Unique per sender run; receiver resets on change.
	std::uint32_t frameId = 0;
	std::uint32_t totalBytes = 0;    ///< Total payload size of the whole frame.
	std::uint32_t payloadOffset = 0; ///< Byte offset of this chunk within the frame.
	std::uint16_t width = 0;
	std::uint16_t height = 0;
	std::uint16_t totalPackets = 0;
	std::uint16_t packetIndex = 0;
	std::uint16_t payloadBytes = 0;
	std::uint8_t format = kFormatRawRgb;
};

/// Header of the mouth/fade state datagram. Immediately followed by
/// lightsW * lightsH * channels bytes of light-grid pixels (row-major, the
/// CPU readback of the mouth's lights FBO). lightsW may be 0 when the sender
/// has no mouth loaded; the fade value is still valid then.
struct StatePacket {
	std::uint32_t magic = kStateMagic;
	std::uint32_t sessionId = 0; ///< Same id as the video stream of this run.
	std::uint32_t sequence = 0;  ///< Monotonic per session; wrapping compare.
	float fade = 0.0f;           ///< Shaped presence fade, 0..1.
	std::uint8_t lightsW = 0;
	std::uint8_t lightsH = 0;
	std::uint8_t channels = 0; ///< Bytes per light (4 = RGBA).
	std::uint8_t reserved[3] = {0, 0, 0};
};
#pragma pack(pop)

constexpr int kHeaderBytes = static_cast<int>(sizeof(PacketHeader));
constexpr int kStateHeaderBytes = static_cast<int>(sizeof(StatePacket));

} // namespace eyestream
