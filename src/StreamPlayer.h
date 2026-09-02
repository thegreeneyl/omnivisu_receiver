#pragma once

#include "ofMain.h"
#include "EyeStreamReceiver.h"

#include <atomic>
#include <condition_variable>
#include <mutex>
#include <string>
#include <vector>

/// Plays back a recorded stream folder (frames/*.jpg + timeline.jsonl, as
/// written by StreamRecorder) on its own worker thread: the timeline parse,
/// the per-frame JPEG disk reads, and the decodes all happen off the main
/// thread, which only polls getLatestFrame()/getLatestState() - the same
/// consume/copy pattern as the live EyeStreamReceiver, so ofApp can treat
/// both sources identically.
///
/// Events are replayed on their recorded timestamps, so the original pacing
/// (including dropped frames) is preserved; mouth targets and the presence
/// fade are re-published as MouthState just like live packets.
class StreamPlayer : public ofThread {
public:
	using MouthState = EyeStreamReceiver::MouthState;

	enum class Status {
		Idle,     ///< No clip loaded (also after stopPlayback()).
		Loading,  ///< startPlayback() accepted; timeline parse in progress.
		Playing,  ///< Events are being replayed.
		Finished  ///< End of timeline reached (or the clip failed to load);
		          ///< the last frame/state stay published for the fade-out.
	};

	~StreamPlayer() override;

	void start();
	void close();

	/// Starts playing the given recording folder (asynchronous; aborts any
	/// clip currently playing). Watch getStatus() for the outcome.
	void startPlayback(const std::string & dirAbs);
	/// Aborts playback and returns to Idle (asynchronous but immediate).
	void stopPlayback();

	Status getStatus() const { return status.load(); }

	/// If a frame newer than the last one returned is ready, copies it into
	/// out and returns true (consume-once, like the live receiver).
	bool getLatestFrame(ofPixels & out);
	/// Copies the most recent replayed state and returns true, or false if
	/// the current clip has not published one yet.
	bool getLatestState(MouthState & out);

private:
	struct Event {
		double t = 0.0;
		bool isFrame = false;
		std::string file; ///< Absolute frame path.
		MouthState state;
	};

	void threadedFunction() override;
	void playFolder(const std::string & dir);
	bool parseTimeline(const std::string & dir, std::vector<Event> & events) const;
	/// Sleeps in short slices until the playback clock reaches t; returns
	/// false when aborted meanwhile.
	bool waitUntil(double startT, double t) const;
	void publishFrame(const ofPixels & pix);
	void publishState(const MouthState & st);

	std::mutex jobMutex;
	std::condition_variable jobCv;
	bool haveJob = false;
	std::string jobDir;
	std::atomic<bool> running{false};
	std::atomic<bool> abortFlag{false};
	std::atomic<Status> status{Status::Idle};

	std::mutex dataMutex;
	ofPixels latestFrame;
	bool newFrameAvailable = false;
	MouthState latestState;
	bool haveState = false;
};
