#include "EyeStreamReceiver.h"

#include "EyeStreamProtocol.h"

#include <cstring>

namespace {
constexpr std::size_t kRecvBufferBytes = 65536;
constexpr std::uint32_t kMaxFrameBytes = 16u * 1024u * 1024u; ///< sanity cap
} // namespace

EyeStreamReceiver::~EyeStreamReceiver() {
	close();
}

//--------------------------------------------------------------
bool EyeStreamReceiver::setup(int port) {
	close();
	listenPort = port;

	if (!udp.Create()) {
		ofLogError("EyeStreamReceiver") << "failed to create UDP socket";
		return false;
	}
	if (!udp.Bind(static_cast<unsigned short>(listenPort))) {
		ofLogError("EyeStreamReceiver") << "failed to bind port " << listenPort;
		udp.Close();
		return false;
	}
	udp.SetReceiveBufferSize(4 * 1024 * 1024);
	// Blocking receive with a short timeout so the worker wakes immediately on
	// data but still polls the running flag for clean shutdown.
	udp.SetNonBlocking(false);
	udp.SetTimeoutReceive(1);

	socketReady = true;
	running = true;
	startThread();

	ofLogNotice("EyeStreamReceiver") << "listening on UDP port " << listenPort;
	return true;
}

//--------------------------------------------------------------
void EyeStreamReceiver::close() {
	running = false;
	if (isThreadRunning()) {
		waitForThread(false, 3000);
	}
	if (socketReady) {
		udp.Close();
		socketReady = false;
	}
}

//--------------------------------------------------------------
bool EyeStreamReceiver::getLatestFrame(ofPixels & out) {
	std::lock_guard<std::mutex> lk(frameMutex);
	if (!newFrameAvailable) {
		return false;
	}
	out = latestFrame;
	newFrameAvailable = false;
	return true;
}

//--------------------------------------------------------------
void EyeStreamReceiver::threadedFunction() {
	std::vector<char> recvBuf(kRecvBufferBytes);

	// In-progress reassembly state (worker thread only).
	bool haveCur = false;
	std::uint32_t curFrameId = 0;
	std::uint32_t curTotalBytes = 0;
	std::uint16_t curTotalPackets = 0;
	std::uint16_t curWidth = 0;
	std::uint16_t curHeight = 0;
	std::uint8_t curFormat = eyestream::kFormatRawRgb;
	std::vector<char> payload;
	std::vector<std::uint8_t> got;
	int gotCount = 0;

	bool haveCompleted = false;
	std::uint32_t lastCompletedId = 0;

	// Identifies the sender run we're currently locked onto. frameId is only
	// monotonic within a session and restarts at 0 when the sender restarts, so
	// a change here means "new sender" and we must drop all prior ordering state.
	bool haveSession = false;
	std::uint32_t curSessionId = 0;

	ofPixels jpegPixels;

	while (running) {
		const int n = udp.Receive(recvBuf.data(), static_cast<int>(recvBuf.size()));
		if (n < eyestream::kHeaderBytes) {
			continue; // timeout (<0), empty, or runt packet
		}

		eyestream::PacketHeader h;
		std::memcpy(&h, recvBuf.data(), eyestream::kHeaderBytes);
		if (h.magic != eyestream::kMagic) {
			continue;
		}
		if (n != eyestream::kHeaderBytes + static_cast<int>(h.payloadBytes)) {
			continue;
		}
		if (h.totalBytes == 0 || h.totalBytes > kMaxFrameBytes || h.totalPackets == 0) {
			continue;
		}

		// A new sender run (different sessionId) restarts frameId at 0. Without
		// this reset, those low frameIds would compare as "older" than the last
		// completed frame and be dropped forever, leaving the last frame frozen.
		if (!haveSession || h.sessionId != curSessionId) {
			haveSession = true;
			curSessionId = h.sessionId;
			haveCompleted = false;
			haveCur = false;
			gotCount = 0;
		}

		// Drop packets for frames we've already completed or that are older.
		if (haveCompleted && static_cast<std::int32_t>(h.frameId - lastCompletedId) <= 0) {
			continue;
		}

		const bool isNewer = !haveCur || static_cast<std::int32_t>(h.frameId - curFrameId) > 0;
		if (isNewer && (!haveCur || h.frameId != curFrameId)) {
			// Begin a new frame, abandoning any older incomplete one.
			if (haveCur && gotCount < curTotalPackets) {
				droppedCount.fetch_add(1);
			}
			haveCur = true;
			curFrameId = h.frameId;
			curTotalBytes = h.totalBytes;
			curTotalPackets = h.totalPackets;
			curWidth = h.width;
			curHeight = h.height;
			curFormat = h.format;
			payload.assign(curTotalBytes, 0);
			got.assign(curTotalPackets, 0);
			gotCount = 0;
		} else if (!haveCur || h.frameId != curFrameId) {
			continue; // older-than-current but newer-than-completed: ignore
		}

		// Place this chunk.
		if (h.packetIndex >= curTotalPackets) {
			continue;
		}
		if (static_cast<std::size_t>(h.payloadOffset) + h.payloadBytes > payload.size()) {
			continue;
		}
		std::memcpy(payload.data() + h.payloadOffset,
			recvBuf.data() + eyestream::kHeaderBytes, h.payloadBytes);
		if (!got[h.packetIndex]) {
			got[h.packetIndex] = 1;
			++gotCount;
		}

		if (gotCount < curTotalPackets) {
			continue;
		}

		// Frame complete: decode and publish.
		bool decoded = false;
		if (curFormat == eyestream::kFormatRawRgb) {
			if (curTotalBytes == static_cast<std::uint32_t>(curWidth) * curHeight * 3) {
				std::lock_guard<std::mutex> lk(frameMutex);
				latestFrame.setFromPixels(
					reinterpret_cast<const unsigned char *>(payload.data()),
					curWidth, curHeight, OF_PIXELS_RGB);
				newFrameAvailable = true;
				decoded = true;
			}
		} else if (curFormat == eyestream::kFormatJpeg) {
			ofBuffer buf(payload.data(), payload.size());
			if (ofLoadImage(jpegPixels, buf)) {
				std::lock_guard<std::mutex> lk(frameMutex);
				latestFrame = jpegPixels;
				newFrameAvailable = true;
				decoded = true;
			}
		}

		if (decoded) {
			frameCount.fetch_add(1);
		} else {
			droppedCount.fetch_add(1);
		}

		haveCompleted = true;
		lastCompletedId = curFrameId;
		haveCur = false;
	}
}
