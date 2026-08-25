#pragma once

#include "ofMain.h"
#include "ofxNetwork.h"

#include <atomic>
#include <cstdint>
#include <mutex>
#include <vector>

/// Receives the eye video UDP stream on a worker thread, reassembles each frame
/// from its packets, and publishes the latest fully-received frame to the main
/// thread. Incomplete or stale frames are discarded so playback stays realtime
/// (newest-complete-wins, never blocks for missing packets).
///
/// The same socket also carries the sender's mouth/fade state datagrams
/// (magic "MOUT", one small packet each, no reassembly). They are demuxed by
/// magic, kept latest-wins keyed on (sessionId, sequence) exactly like the
/// video frames, and published via getLatestState().
class EyeStreamReceiver : public ofThread {
public:
	~EyeStreamReceiver() override;

	/// Binds the UDP socket to listenPort and starts the worker thread.
	bool setup(int listenPort);
	void close();

	/// If a frame newer than the last one returned is ready, copies it into
	/// out and returns true. Otherwise returns false and leaves out untouched.
	bool getLatestFrame(ofPixels & out);

	/// Copies the most recent mouth/fade state and returns true, or returns
	/// false if no state packet has arrived yet. Unlike getLatestFrame this
	/// always hands out the latest state (the caller wants the current fade
	/// every render frame, not only on change). lights may come back
	/// unallocated when the sender has no mouth loaded.
	bool getLatestState(float & fade, ofPixels & lights);

	std::uint64_t getFrameCount() const { return frameCount.load(); }
	std::uint64_t getDroppedCount() const { return droppedCount.load(); }
	std::uint64_t getStateCount() const { return stateCount.load(); }

private:
	void threadedFunction() override;

	int listenPort = 12345;
	ofxUDPManager udp;
	bool socketReady = false;
	std::atomic<bool> running{false};

	std::mutex frameMutex;
	ofPixels latestFrame;
	bool newFrameAvailable = false;

	// Latest mouth/fade state (shared with the main thread under frameMutex;
	// the payload is tiny so one mutex for both channels is fine).
	float latestStateFade = 0.0f;
	ofPixels latestStateLights;
	bool haveState = false;

	std::atomic<std::uint64_t> frameCount{0};
	std::atomic<std::uint64_t> droppedCount{0};
	std::atomic<std::uint64_t> stateCount{0};
};
