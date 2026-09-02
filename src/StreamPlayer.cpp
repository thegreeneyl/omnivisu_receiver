#include "StreamPlayer.h"

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <thread>

namespace fs = std::filesystem;

namespace {
double nowSeconds() {
	return static_cast<double>(ofGetElapsedTimeMicros()) / 1000000.0;
}
} // namespace

StreamPlayer::~StreamPlayer() {
	close();
}

//--------------------------------------------------------------
void StreamPlayer::start() {
	if (isThreadRunning()) {
		return;
	}
	running = true;
	startThread();
}

//--------------------------------------------------------------
void StreamPlayer::close() {
	running = false;
	abortFlag = true;
	jobCv.notify_all();
	if (isThreadRunning()) {
		waitForThread(false, 5000);
	}
}

//--------------------------------------------------------------
void StreamPlayer::startPlayback(const std::string & dirAbs) {
	{
		std::lock_guard<std::mutex> lk(jobMutex);
		jobDir = dirAbs;
		haveJob = true;
	}
	status = Status::Loading;
	abortFlag = true; // kill a clip that is still playing
	jobCv.notify_one();
}

//--------------------------------------------------------------
void StreamPlayer::stopPlayback() {
	{
		std::lock_guard<std::mutex> lk(jobMutex);
		haveJob = false; // also cancel a not-yet-started job
	}
	abortFlag = true;
	status = Status::Idle;
	std::lock_guard<std::mutex> lk(dataMutex);
	newFrameAvailable = false;
	haveState = false;
}

//--------------------------------------------------------------
bool StreamPlayer::getLatestFrame(ofPixels & out) {
	std::lock_guard<std::mutex> lk(dataMutex);
	if (!newFrameAvailable) {
		return false;
	}
	out = latestFrame;
	newFrameAvailable = false;
	return true;
}

//--------------------------------------------------------------
bool StreamPlayer::getLatestState(MouthState & out) {
	std::lock_guard<std::mutex> lk(dataMutex);
	if (!haveState) {
		return false;
	}
	out = latestState;
	return true;
}

//--------------------------------------------------------------
void StreamPlayer::threadedFunction() {
	while (running) {
		std::string dir;
		{
			std::unique_lock<std::mutex> lk(jobMutex);
			jobCv.wait(lk, [this] { return !running || haveJob; });
			if (!running) {
				return;
			}
			dir = jobDir;
			haveJob = false;
		}
		abortFlag = false;
		playFolder(dir);
	}
}

//--------------------------------------------------------------
void StreamPlayer::playFolder(const std::string & dir) {
	status = Status::Loading;
	{
		// A fresh clip starts with no published data: the main thread blanks
		// its frame on source switch and waits for the first replayed one.
		std::lock_guard<std::mutex> lk(dataMutex);
		newFrameAvailable = false;
		haveState = false;
	}

	std::vector<Event> events;
	if (!parseTimeline(dir, events)) {
		ofLogWarning("StreamPlayer") << "cannot play " << dir
			<< " (missing/empty timeline or no frames)";
		status = Status::Finished;
		return;
	}

	ofLogNotice("StreamPlayer") << "playback started: " << dir
		<< " (" << events.size() << " events)";
	status = Status::Playing;
	const double startT = nowSeconds();

	ofPixels pix;
	for (const auto & ev : events) {
		if (abortFlag || !running) {
			break;
		}
		if (ev.isFrame) {
			// Decode first (overlaps the inter-frame gap), then wait for the
			// event's timestamp, then publish - original pacing preserved.
			if (!ofLoadImage(pix, ev.file)) {
				ofLogWarning("StreamPlayer") << "failed to load " << ev.file;
				continue;
			}
			if (!waitUntil(startT, ev.t)) {
				break;
			}
			publishFrame(pix);
		} else {
			if (!waitUntil(startT, ev.t)) {
				break;
			}
			publishState(ev.state);
		}
	}

	if (abortFlag || !running) {
		status = Status::Idle;
	} else {
		ofLogNotice("StreamPlayer") << "playback finished: " << dir;
		status = Status::Finished;
	}
}

//--------------------------------------------------------------
bool StreamPlayer::parseTimeline(const std::string & dir, std::vector<Event> & events) const {
	std::ifstream in(fs::path(dir) / "timeline.jsonl");
	if (!in.is_open()) {
		return false;
	}

	bool anyFrame = false;
	std::string line;
	while (std::getline(in, line)) {
		if (line.empty()) {
			continue;
		}
		const ofJson j = ofJson::parse(line, nullptr, false);
		if (j.is_discarded() || !j.is_object()) {
			continue;
		}
		Event ev;
		ev.t = j.value("t", 0.0);
		const std::string type = j.value("type", std::string());
		if (type == "frame") {
			const std::string rel = j.value("file", std::string());
			if (rel.empty()) {
				continue;
			}
			ev.isFrame = true;
			ev.file = (fs::path(dir) / rel).string();
			anyFrame = true;
		} else if (type == "state") {
			ev.isFrame = false;
			ev.state.fade = std::min(1.0f, std::max(0.0f, j.value("fade", 0.0f)));
			ev.state.hasTarget = j.value("hasTarget", false);
			ev.state.lightsW = j.value("w", 0);
			ev.state.lightsH = j.value("h", 0);
			ev.state.targetLeft = j.value("left", 0.0f);
			ev.state.targetRight = j.value("right", 0.0f);
		} else {
			continue;
		}
		events.push_back(std::move(ev));
	}

	// The recorder appends in arrival order, which is already time-ordered;
	// a stable sort guards against any manual edits of the file.
	std::stable_sort(events.begin(), events.end(),
		[](const Event & a, const Event & b) { return a.t < b.t; });
	return anyFrame;
}

//--------------------------------------------------------------
bool StreamPlayer::waitUntil(double startT, double t) const {
	while (nowSeconds() - startT < t) {
		if (abortFlag || !running) {
			return false;
		}
		std::this_thread::sleep_for(std::chrono::milliseconds(1));
	}
	return !(abortFlag || !running);
}

//--------------------------------------------------------------
void StreamPlayer::publishFrame(const ofPixels & pix) {
	std::lock_guard<std::mutex> lk(dataMutex);
	latestFrame = pix;
	newFrameAvailable = true;
}

//--------------------------------------------------------------
void StreamPlayer::publishState(const MouthState & st) {
	std::lock_guard<std::mutex> lk(dataMutex);
	latestState = st;
	haveState = true;
}
