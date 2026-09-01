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
/// StatePacket.flags bit: the target edge fields are valid (the sender has a
/// mouth loaded). Without it the receiver falls back to its local neutral.
constexpr std::uint8_t kStateFlagHasTarget = 0x01;

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

/// The mouth/fade state datagram: one fixed-size packet, no payload. The
/// mouth is carried as the sender's QUANTIZED target edges (whole lights of
/// the sender's grid, right edge exclusive) instead of baked pixels: the
/// receiver owns the presentation (easing, neutral idle pose, rasterization)
/// so its animation parameters can be tuned - and recorded targets replayed -
/// without touching the sender. The fade applies to the EYES only; the mouth
/// is always shown by the receiver.
struct StatePacket {
	std::uint32_t magic = kStateMagic;
	std::uint32_t sessionId = 0; ///< Same id as the video stream of this run.
	std::uint32_t sequence = 0;  ///< Monotonic per session; wrapping compare.
	float fade = 0.0f;           ///< Shaped presence fade for the eyes, 0..1.
	std::uint8_t lightsW = 0;    ///< Sender's light-grid size (e.g. 14x1);
	std::uint8_t lightsH = 0;    ///< 0 when the sender has no mouth loaded.
	std::uint8_t flags = 0;      ///< kStateFlagHasTarget when edges are valid.
	std::uint8_t reserved = 0;
	float targetLeft = 0.0f;     ///< Target left edge in light units (integer-valued).
	float targetRight = 0.0f;    ///< Target right edge, exclusive.
};
#pragma pack(pop)

constexpr int kHeaderBytes = static_cast<int>(sizeof(PacketHeader));
constexpr int kStateHeaderBytes = static_cast<int>(sizeof(StatePacket));

} // namespace eyestream
