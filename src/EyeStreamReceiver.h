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
class EyeStreamReceiver : public ofThread {
public:
	~EyeStreamReceiver() override;

	/// Binds the UDP socket to listenPort and starts the worker thread.
	bool setup(int listenPort);
	void close();

	/// If a frame newer than the last one returned is ready, copies it into
	/// out and returns true. Otherwise returns false and leaves out untouched.
	bool getLatestFrame(ofPixels & out);

	std::uint64_t getFrameCount() const { return frameCount.load(); }
	std::uint64_t getDroppedCount() const { return droppedCount.load(); }

private:
	void threadedFunction() override;

	int listenPort = 12345;
	ofxUDPManager udp;
	bool socketReady = false;
	std::atomic<bool> running{false};

	std::mutex frameMutex;
	ofPixels latestFrame;
	bool newFrameAvailable = false;

	std::atomic<std::uint64_t> frameCount{0};
	std::atomic<std::uint64_t> droppedCount{0};
};
