#pragma once

#include "ofMain.h"

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <fstream>
#include <mutex>
#include <string>
#include <vector>

/// Writes a camera stream to disk on its own worker thread so neither the
/// UDP receive thread nor the render thread ever touches the filesystem.
///
/// A recording is a folder containing
///   frames/000001.jpg ...   one JPEG per completed video frame (the wire
///                           payload is written as-is when it is already JPEG;
///                           raw-RGB frames are encoded here on the worker)
///   timeline.jsonl          append-only frame/state events with timestamps
///                           relative to the recording start
///   manifest.json           written on finalize: frame count, duration,
///                           start timestamp, payload bytes
///
/// Everything runs through ONE ordered job queue: frame/state data items are
/// pushed by the UDP thread (enqueue-only, bounded, drop-and-count on
/// overflow), commands (start/finalize/resolve/discard) by the main thread.
/// The strict ordering is what makes the lifecycle race-free: a
/// resolveLastRecording() enqueued after finalizeRecording() is guaranteed to
/// act on exactly that recording, even if a new recording has already been
/// started behind it.
class StreamRecorder : public ofThread {
public:
	/// Published after a Finalize job completed; the main thread polls this
	/// (keyed on the id returned by startRecording) to decide whether the
	/// clip is playable (frameCount > 0).
	struct Result {
		std::uint64_t id = 0;
		std::string dir;
		std::string timestampName; ///< Human-readable start time, e.g. "2026-09-02_10-45-12".
		int frameCount = 0;
		double durationSeconds = 0.0;
	};

	~StreamRecorder() override;

	void start();
	/// Drains the queue (a Quit job is processed after all pending writes),
	/// finalizes a still-open recording, and joins the thread.
	void close();

	// --- data producers (called from the UDP receive thread) ---
	// All three only copy the data into the queue and return; they are no-ops
	// while no recording is accepting (cheap atomic check).
	void pushJpegFrame(double t, const char * bytes, std::size_t numBytes);
	void pushRawFrame(double t, const unsigned char * rgb, int w, int h);
	void pushState(double t, float fade, bool hasTarget, int lightsW, int lightsH,
		float targetLeft, float targetRight);

	// --- commands (called from the main thread; all asynchronous) ---
	/// Starts a new recording into dirAbs (wiped and recreated). Returns the
	/// recording id used to poll getResult after finalizeRecording().
	std::uint64_t startRecording(const std::string & dirAbs);
	/// Stops accepting data immediately and queues the manifest write.
	void finalizeRecording();
	/// Promotes (keepPermanent, with a free-space gate) or deletes the most
	/// recently finalized recording. Empty recordings are always deleted.
	/// Must be called exactly once per finalized recording.
	void resolveLastRecording(bool keepPermanent, const std::string & permanentParentAbs,
		double minFreeGb);
	/// Deletes a folder (temp-slot cleanup at startup).
	void discard(const std::string & dirAbs);

	/// True once the recording with the given id has been finalized.
	bool getResult(std::uint64_t id, Result & out);

	std::uint64_t getDroppedItems() const { return droppedItems.load(); }

private:
	enum class ItemType { JpegFrame, RawFrame, State, Start, Finalize, Resolve, Discard, Quit };

	struct Item {
		ItemType type = ItemType::State;
		double t = 0.0;
		std::vector<char> bytes; ///< JPEG payload (JpegFrame).
		ofPixels pixels;         ///< RGB frame (RawFrame).
		float fade = 0.0f;
		bool hasTarget = false;
		int lightsW = 0;
		int lightsH = 0;
		float targetLeft = 0.0f;
		float targetRight = 0.0f;
		std::string dir;       ///< Start/Discard target, Resolve permanent parent.
		std::string name;      ///< Start: human-readable timestamp.
		bool keepPermanent = false;
		double minFreeGb = 0.0;
		std::uint64_t id = 0;
	};

	void threadedFunction() override;
	void enqueue(Item && item, bool isFrame);
	void handleStart(const Item & item);
	void handleFrame(Item & item);
	void handleState(const Item & item);
	void finalizeOpenRecording();
	void handleResolve(const Item & item);
	/// Free-space-gated move into parent/<name> (suffixing on collision);
	/// falls back to copy+delete across volumes. Deletes src when the gate
	/// fails so the temp space is always reclaimed.
	void promoteDir(const std::string & srcDir, const std::string & parentDir,
		const std::string & name, double minFreeGb);
	static void removeDir(const std::string & dir);

	std::mutex queueMutex;
	std::condition_variable queueCv;
	std::deque<Item> queue;
	int framesQueued = 0;

	std::atomic<bool> accepting{false};
	std::atomic<std::uint64_t> droppedItems{0};
	std::atomic<std::uint64_t> idCounter{0};

	std::mutex resultMutex;
	Result lastResult;
	bool haveResult = false;

	// --- worker-thread-only recording state ---
	bool recOpen = false;
	std::uint64_t recId = 0;
	std::string recDir;
	std::string recName;
	double recStartT = 0.0;
	double recLastT = 0.0;
	int recFrames = 0;
	std::uint64_t recBytes = 0;
	std::ofstream timeline;

	// Last finalized-but-unresolved recording (worker-thread-only); consumed
	// by the Resolve job.
	bool haveLastFinalized = false;
	Result lastFinalized;
};
