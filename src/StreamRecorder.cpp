#include "StreamRecorder.h"

#include <algorithm>
#include <cstdio>
#include <filesystem>

namespace fs = std::filesystem;

namespace {
/// Upper bound of frame items waiting for the writer. ~10 s at 50 fps: if the
/// disk stalls longer than that, frames are dropped (and counted) instead of
/// growing the queue without bound or ever blocking the UDP thread.
constexpr int kMaxQueuedFrames = 512;

double dirSizeBytes(const fs::path & dir) {
	double total = 0.0;
	std::error_code ec;
	for (fs::recursive_directory_iterator it(dir, ec), end; !ec && it != end; it.increment(ec)) {
		if (it->is_regular_file(ec)) {
			total += static_cast<double>(it->file_size(ec));
		}
	}
	return total;
}
} // namespace

StreamRecorder::~StreamRecorder() {
	close();
}

//--------------------------------------------------------------
void StreamRecorder::start() {
	if (isThreadRunning()) {
		return;
	}
	startThread();
}

//--------------------------------------------------------------
void StreamRecorder::close() {
	if (!isThreadRunning()) {
		return;
	}
	accepting = false;
	Item quit;
	quit.type = ItemType::Quit;
	enqueue(std::move(quit), false);
	waitForThread(false, 10000);
}

//--------------------------------------------------------------
void StreamRecorder::enqueue(Item && item, bool isFrame) {
	{
		std::lock_guard<std::mutex> lk(queueMutex);
		if (isFrame) {
			if (framesQueued >= kMaxQueuedFrames) {
				droppedItems.fetch_add(1);
				return;
			}
			++framesQueued;
		}
		queue.push_back(std::move(item));
	}
	queueCv.notify_one();
}

//--------------------------------------------------------------
void StreamRecorder::pushJpegFrame(double t, const char * bytes, std::size_t numBytes) {
	if (!accepting.load() || bytes == nullptr || numBytes == 0) {
		return;
	}
	Item item;
	item.type = ItemType::JpegFrame;
	item.t = t;
	item.bytes.assign(bytes, bytes + numBytes);
	enqueue(std::move(item), true);
}

//--------------------------------------------------------------
void StreamRecorder::pushRawFrame(double t, const unsigned char * rgb, int w, int h) {
	if (!accepting.load() || rgb == nullptr || w <= 0 || h <= 0) {
		return;
	}
	Item item;
	item.type = ItemType::RawFrame;
	item.t = t;
	item.pixels.setFromPixels(rgb, w, h, OF_PIXELS_RGB);
	enqueue(std::move(item), true);
}

//--------------------------------------------------------------
void StreamRecorder::pushState(double t, float fade, bool hasTarget, int lightsW,
	int lightsH, float targetLeft, float targetRight) {
	if (!accepting.load()) {
		return;
	}
	Item item;
	item.type = ItemType::State;
	item.t = t;
	item.fade = fade;
	item.hasTarget = hasTarget;
	item.lightsW = lightsW;
	item.lightsH = lightsH;
	item.targetLeft = targetLeft;
	item.targetRight = targetRight;
	enqueue(std::move(item), false);
}

//--------------------------------------------------------------
std::uint64_t StreamRecorder::startRecording(const std::string & dirAbs) {
	const std::uint64_t id = idCounter.fetch_add(1) + 1;
	Item item;
	item.type = ItemType::Start;
	item.t = static_cast<double>(ofGetElapsedTimeMicros()) / 1000000.0;
	item.dir = dirAbs;
	item.name = ofGetTimestampString("%Y-%m-%d_%H-%M-%S");
	item.id = id;
	enqueue(std::move(item), false);
	// Accept data from now on; anything the UDP thread enqueued BEFORE the
	// Start job lands ahead of it in the queue and is dropped by the worker
	// (at most a frame or two around the stream-start detection).
	accepting = true;
	return id;
}

//--------------------------------------------------------------
void StreamRecorder::finalizeRecording(double trimEndSeconds) {
	accepting = false;
	Item item;
	item.type = ItemType::Finalize;
	item.trimSeconds = std::max(0.0, trimEndSeconds);
	enqueue(std::move(item), false);
}

//--------------------------------------------------------------
void StreamRecorder::resolveLastRecording(bool keepPermanent,
	const std::string & permanentParentAbs, double minFreeGb, double minClipSeconds) {
	Item item;
	item.type = ItemType::Resolve;
	item.keepPermanent = keepPermanent;
	item.dir = permanentParentAbs;
	item.minFreeGb = minFreeGb;
	item.minClipSeconds = minClipSeconds;
	enqueue(std::move(item), false);
}

//--------------------------------------------------------------
void StreamRecorder::discard(const std::string & dirAbs) {
	Item item;
	item.type = ItemType::Discard;
	item.dir = dirAbs;
	enqueue(std::move(item), false);
}

//--------------------------------------------------------------
bool StreamRecorder::getResult(std::uint64_t id, Result & out) {
	std::lock_guard<std::mutex> lk(resultMutex);
	if (!haveResult || lastResult.id != id) {
		return false;
	}
	out = lastResult;
	return true;
}

//--------------------------------------------------------------
void StreamRecorder::threadedFunction() {
	while (true) {
		Item item;
		{
			std::unique_lock<std::mutex> lk(queueMutex);
			queueCv.wait(lk, [this] { return !queue.empty(); });
			item = std::move(queue.front());
			queue.pop_front();
			if (item.type == ItemType::JpegFrame || item.type == ItemType::RawFrame) {
				--framesQueued;
			}
		}

		switch (item.type) {
		case ItemType::Start:
			handleStart(item);
			break;
		case ItemType::JpegFrame:
		case ItemType::RawFrame:
			handleFrame(item);
			break;
		case ItemType::State:
			handleState(item);
			break;
		case ItemType::Finalize:
			finalizeOpenRecording(item.trimSeconds);
			break;
		case ItemType::Resolve:
			handleResolve(item);
			break;
		case ItemType::Discard:
			removeDir(item.dir);
			break;
		case ItemType::Quit:
			// Everything queued before the Quit has been written; make sure a
			// recording that was never finalized (unexpected shutdown path)
			// still gets its manifest. No trim on this safety path - the clip
			// is preserved as-is for inspection.
			finalizeOpenRecording(0.0);
			return;
		}
	}
}

//--------------------------------------------------------------
void StreamRecorder::handleStart(const Item & item) {
	finalizeOpenRecording(0.0); // safety: never leave a recording without manifest

	std::error_code ec;
	fs::remove_all(item.dir, ec);
	fs::create_directories(fs::path(item.dir) / "frames", ec);
	if (ec) {
		ofLogError("StreamRecorder") << "cannot create " << item.dir << ": " << ec.message();
	}

	timeline.open(fs::path(item.dir) / "timeline.jsonl",
		std::ios::out | std::ios::trunc);
	if (!timeline.is_open()) {
		ofLogError("StreamRecorder") << "cannot open timeline in " << item.dir;
	}

	recOpen = true;
	recId = item.id;
	recDir = item.dir;
	recName = item.name;
	recStartT = item.t;
	recLastT = 0.0;
	recLastFrameT = 0.0;
	recFrames = 0;
	recBytes = 0;
	ofLogNotice("StreamRecorder") << "recording started: " << item.dir
		<< " (" << recName << ")";
}

//--------------------------------------------------------------
void StreamRecorder::handleFrame(Item & item) {
	if (!recOpen) {
		return; // data enqueued outside a recording window
	}

	// Raw RGB is encoded here on the worker; JPEG payloads are written as-is.
	if (item.type == ItemType::RawFrame) {
		ofBuffer encoded;
		if (!ofSaveImage(item.pixels, encoded, OF_IMAGE_FORMAT_JPEG, OF_IMAGE_QUALITY_HIGH)) {
			ofLogWarning("StreamRecorder") << "JPEG encode failed; frame dropped";
			return;
		}
		item.bytes.assign(encoded.getData(), encoded.getData() + encoded.size());
	}

	const double t = std::max(0.0, item.t - recStartT);
	char fname[32];
	std::snprintf(fname, sizeof(fname), "%06d.jpg", recFrames + 1);
	const std::string rel = std::string("frames/") + fname;

	std::ofstream out(fs::path(recDir) / rel, std::ios::out | std::ios::binary);
	if (!out.is_open()) {
		ofLogWarning("StreamRecorder") << "cannot write " << rel << "; frame dropped";
		return;
	}
	out.write(item.bytes.data(), static_cast<std::streamsize>(item.bytes.size()));
	out.close();

	if (timeline.is_open()) {
		ofJson j;
		j["t"] = t;
		j["type"] = "frame";
		j["file"] = rel;
		timeline << j.dump() << "\n";
	}

	++recFrames;
	recBytes += item.bytes.size();
	recLastT = std::max(recLastT, t);
	recLastFrameT = std::max(recLastFrameT, t);
}

//--------------------------------------------------------------
void StreamRecorder::handleState(const Item & item) {
	if (!recOpen || !timeline.is_open()) {
		return;
	}
	const double t = std::max(0.0, item.t - recStartT);
	ofJson j;
	j["t"] = t;
	j["type"] = "state";
	j["fade"] = item.fade;
	j["hasTarget"] = item.hasTarget;
	j["left"] = item.targetLeft;
	j["right"] = item.targetRight;
	j["w"] = item.lightsW;
	j["h"] = item.lightsH;
	timeline << j.dump() << "\n";
	recLastT = std::max(recLastT, t);
}

//--------------------------------------------------------------
void StreamRecorder::finalizeOpenRecording(double trimSeconds) {
	if (!recOpen) {
		return;
	}
	if (timeline.is_open()) {
		timeline.close();
	}

	// Cut the configured tail (the person walking away + fade-out) BEFORE the
	// manifest/result, so playback, the min-duration storage gate, and the
	// archive all see only the trimmed clip. Runs on this worker thread, so
	// the render thread never pays for the file deletes/rewrite.
	if (trimSeconds > 0.0 && recFrames > 0) {
		trimRecordingTail(trimSeconds);
	}

	ofJson manifest;
	manifest["started"] = recName;
	manifest["frames"] = recFrames;
	manifest["duration_seconds"] = recLastT;
	manifest["video_seconds"] = recLastFrameT;
	manifest["payload_bytes"] = recBytes;
	if (trimSeconds > 0.0) {
		manifest["trimmed_end_seconds"] = trimSeconds;
	}
	ofSavePrettyJson(fs::path(recDir) / "manifest.json", manifest);

	Result res;
	res.id = recId;
	res.dir = recDir;
	res.timestampName = recName;
	res.frameCount = recFrames;
	res.durationSeconds = recLastT;
	res.videoSeconds = recLastFrameT;

	{
		std::lock_guard<std::mutex> lk(resultMutex);
		lastResult = res;
		haveResult = true;
	}
	lastFinalized = res;
	haveLastFinalized = true;
	recOpen = false;

	ofLogNotice("StreamRecorder") << "recording finalized: " << recDir
		<< " frames=" << recFrames
		<< " duration=" << ofToString(recLastT, 2) << "s";
}

//--------------------------------------------------------------
void StreamRecorder::trimRecordingTail(double trimSeconds) {
	const double keepT = recLastFrameT - trimSeconds;
	const fs::path dir(recDir);

	if (keepT <= 0.0) {
		// The trim swallows the whole video: finalize as an empty clip, so
		// the main thread skips the playback and the resolve deletes the
		// folder (its frameCount <= 0 path) - no playback, no storage.
		ofLogNotice("StreamRecorder") << "end-trim " << ofToString(trimSeconds, 2)
			<< "s >= recorded video " << ofToString(recLastFrameT, 2)
			<< "s - clip skipped (no playback, no storage): " << recDir;
		recFrames = 0;
		recLastT = 0.0;
		recLastFrameT = 0.0;
		recBytes = 0;
		return;
	}

	const fs::path timelinePath = dir / "timeline.jsonl";
	std::ifstream in(timelinePath);
	if (!in.is_open()) {
		ofLogWarning("StreamRecorder") << "cannot open timeline for trimming in "
			<< recDir << " - clip left untrimmed";
		return;
	}

	// First pass: split the timeline at the cutoff. Kept events stay as they
	// are; frame files past the cutoff are deleted; state events past the
	// cutoff are collected - they hold the recorded fade-out (the person
	// walking away), which is shifted forward by the trimmed amount below so
	// the clip still ends on its own fade-out.
	const int origFrames = recFrames;
	const double origVideoT = recLastFrameT;
	std::vector<ofJson> kept;
	std::vector<ofJson> tailStates;
	int keptFrames = 0;
	double lastKeptFrameT = 0.0;
	std::uint64_t removedBytes = 0;
	std::string line;
	while (std::getline(in, line)) {
		if (line.empty()) {
			continue;
		}
		ofJson j = ofJson::parse(line, nullptr, false);
		if (j.is_discarded() || !j.is_object()) {
			continue;
		}
		const double t = j.value("t", 0.0);
		const bool isFrame = j.value("type", std::string()) == "frame";
		if (t > keepT) {
			if (isFrame) {
				const std::string rel = j.value("file", std::string());
				if (!rel.empty()) {
					std::error_code ec;
					const fs::path f = dir / rel;
					const auto sz = fs::file_size(f, ec);
					if (!ec) {
						removedBytes += static_cast<std::uint64_t>(sz);
					}
					fs::remove(f, ec);
				}
			} else {
				tailStates.push_back(std::move(j));
			}
			continue;
		}
		kept.push_back(std::move(j));
		if (isFrame) {
			++keptFrames;
			lastKeptFrameT = std::max(lastKeptFrameT, t);
		}
	}
	in.close();

	// The recorder appends in arrival order (already time-ordered); a stable
	// sort guards against any out-of-order writes, same as the player does.
	std::stable_sort(tailStates.begin(), tailStates.end(),
		[](const ofJson & a, const ofJson & b) {
			return a.value("t", 0.0) < b.value("t", 0.0);
		});

	const fs::path tmpPath = dir / "timeline.jsonl.tmp";
	std::ofstream out(tmpPath, std::ios::out | std::ios::trunc);
	if (!out.is_open()) {
		ofLogWarning("StreamRecorder") << "cannot write " << tmpPath.string()
			<< " - clip left untrimmed";
		return;
	}

	// Second pass: write the kept events, moving the cut-away fade envelope
	// forward by trimSeconds. Kept states inside the final trim window get
	// their FADE overridden by the shifted tail fade at their time (step-wise,
	// matching playback semantics) while their mouth targets stay untouched,
	// so the mouth keeps matching the visible video. Nothing before that
	// window changes.
	double lastKeptT = 0.0;
	std::size_t tailIdx = 0;
	float tailFade = -1.0f; // shifted tail fade at/before the current time
	for (auto & j : kept) {
		const double t = j.value("t", 0.0);
		if (j.value("type", std::string()) == "state" && t > keepT - trimSeconds) {
			while (tailIdx < tailStates.size()
				&& tailStates[tailIdx].value("t", 0.0) - trimSeconds <= t) {
				tailFade = tailStates[tailIdx].value("fade", 0.0f);
				++tailIdx;
			}
			if (tailFade >= 0.0f) {
				j["fade"] = tailFade;
			}
		}
		out << j.dump() << "\n";
		lastKeptT = std::max(lastKeptT, t);
	}
	// Tail states landing past the last kept frame are re-appended at their
	// shifted times: the end of the fade-out plus the trailing fade-0 states,
	// exactly like the tail of an untrimmed recording.
	for (auto & j : tailStates) {
		const double shiftedT = j.value("t", 0.0) - trimSeconds;
		if (shiftedT <= keepT) {
			continue; // already consumed as a fade override above
		}
		j["t"] = shiftedT;
		out << j.dump() << "\n";
		lastKeptT = std::max(lastKeptT, shiftedT);
	}
	out.close();

	std::error_code ec;
	fs::rename(tmpPath, timelinePath, ec);
	if (ec) {
		ofLogWarning("StreamRecorder") << "failed to replace trimmed timeline in "
			<< recDir << ": " << ec.message();
	}

	recFrames = keptFrames;
	recLastT = lastKeptT;
	recLastFrameT = lastKeptFrameT;
	recBytes = (removedBytes <= recBytes) ? recBytes - removedBytes : 0;

	ofLogNotice("StreamRecorder") << "end-trim " << ofToString(trimSeconds, 2)
		<< "s applied: video " << ofToString(origVideoT, 2) << "s -> "
		<< ofToString(recLastFrameT, 2) << "s, frames " << origFrames
		<< " -> " << keptFrames;
}

//--------------------------------------------------------------
void StreamRecorder::handleResolve(const Item & item) {
	if (!haveLastFinalized) {
		return; // nothing to resolve (e.g. resolve after an empty session)
	}
	const Result res = lastFinalized;
	haveLastFinalized = false;

	if (!item.keepPermanent || res.frameCount <= 0) {
		removeDir(res.dir);
		ofLogNotice("StreamRecorder") << "temp recording discarded: " << res.dir
			<< (res.frameCount <= 0 ? " (empty)" : "");
		return;
	}
	// Min-length gate: clips shorter than the threshold never reach the
	// permanent archive (someone only glancing at the camera).
	if (res.videoSeconds < item.minClipSeconds) {
		removeDir(res.dir);
		ofLogNotice("StreamRecorder") << "temp recording discarded: " << res.dir
			<< " (video " << ofToString(res.videoSeconds, 2) << "s < min "
			<< ofToString(item.minClipSeconds, 2) << "s)";
		return;
	}
	promoteDir(res.dir, item.dir, res.timestampName, item.minFreeGb);
}

//--------------------------------------------------------------
void StreamRecorder::promoteDir(const std::string & srcDir, const std::string & parentDir,
	const std::string & name, double minFreeGb) {
	std::error_code ec;
	fs::create_directories(parentDir, ec);

	// Free-space gate: keeping the clip means its bytes stay allocated, so
	// require headroom for the clip on top of the configured reserve (for
	// temp recordings and system operations). On failure the temp copy is
	// deleted so its space is always reclaimed.
	const double srcBytes = dirSizeBytes(srcDir);
	const auto space = fs::space(parentDir, ec);
	const double availableBytes = ec ? -1.0 : static_cast<double>(space.available);
	const double minBytes = minFreeGb * 1e9;
	if (availableBytes >= 0.0 && availableBytes < minBytes + srcBytes) {
		ofLogWarning("StreamRecorder") << "not enough free space to keep recording ("
			<< ofToString(availableBytes / 1e9, 1) << " GB free, need "
			<< ofToString((minBytes + srcBytes) / 1e9, 1)
			<< " GB) - discarding " << srcDir;
		removeDir(srcDir);
		return;
	}

	fs::path dest = fs::path(parentDir) / name;
	for (int suffix = 2; fs::exists(dest, ec); ++suffix) {
		dest = fs::path(parentDir) / (name + "_" + std::to_string(suffix));
	}

	fs::rename(srcDir, dest, ec);
	if (ec) {
		// Different volume (or rename refused): copy, then delete the source.
		ec.clear();
		fs::copy(srcDir, dest, fs::copy_options::recursive, ec);
		if (ec) {
			ofLogError("StreamRecorder") << "failed to store recording to " << dest.string()
				<< ": " << ec.message();
			removeDir(srcDir);
			return;
		}
		removeDir(srcDir);
	}
	ofLogNotice("StreamRecorder") << "recording stored permanently: " << dest.string()
		<< " (" << ofToString(srcBytes / 1e6, 1) << " MB, "
		<< ofToString(availableBytes / 1e9, 1) << " GB free)";
}

//--------------------------------------------------------------
void StreamRecorder::removeDir(const std::string & dir) {
	std::error_code ec;
	fs::remove_all(dir, ec);
	if (ec) {
		ofLogWarning("StreamRecorder") << "failed to remove " << dir << ": " << ec.message();
	}
}
