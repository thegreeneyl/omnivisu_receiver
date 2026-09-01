#include "ArtnetSender.h"

#include <algorithm>
#include <cstring>

namespace {

/// Maximum DMX channels in one ArtDMX packet.
constexpr int kMaxDmxChannels = 512;

bool isValidColorOrder(const std::string & order) {
	if (order.empty() || order.size() > 4) {
		return false;
	}
	return std::all_of(order.begin(), order.end(), [](char c) {
		return c == 'R' || c == 'G' || c == 'B' || c == 'W';
	});
}

} // namespace

//--------------------------------------------------------------
void ArtnetSender::setup(const Config & c) {
	close();
	cfg = c;

	if (!isValidColorOrder(cfg.colorOrder)) {
		ofLogWarning("ArtnetSender") << "invalid color_order '" << cfg.colorOrder
			<< "' (chars R/G/B/W, 1-4 long) - using RGB";
		cfg.colorOrder = "RGB";
	}

	// Start corner: whether the traversal begins at the top or bottom row and
	// whether the first row runs left-to-right or right-to-left.
	if (cfg.startCorner == "top_left") {
		fromTop = true;
		fromLeft = true;
	} else if (cfg.startCorner == "top_right") {
		fromTop = true;
		fromLeft = false;
	} else if (cfg.startCorner == "bottom_left") {
		fromTop = false;
		fromLeft = true;
	} else if (cfg.startCorner == "bottom_right") {
		fromTop = false;
		fromLeft = false;
	} else {
		ofLogWarning("ArtnetSender") << "unknown start_corner '" << cfg.startCorner
			<< "' - using top_left";
		cfg.startCorner = "top_left";
		fromTop = true;
		fromLeft = true;
	}

	cfg.startChannel = std::clamp(cfg.startChannel, 1, kMaxDmxChannels);
	if (cfg.universes.empty()) {
		cfg.universes.push_back(0);
	}
	for (int & u : cfg.universes) {
		u = std::clamp(u, 0, 0x7FFF);
	}
	warnedOverflow = false;

	if (!cfg.enabled) {
		ofLogNotice("ArtnetSender") << "disabled in config";
		return;
	}

	udp.Create();
	if (!udp.Connect(cfg.ip.c_str(), cfg.port)) {
		ofLogError("ArtnetSender") << "failed to connect UDP socket to "
			<< cfg.ip << ":" << cfg.port;
		udp.Close();
		return;
	}
	// Broadcast is the ArtNet default addressing; enabling it always is
	// harmless for unicast IPs.
	udp.SetEnableBroadcast(true);
	udp.SetNonBlocking(true);
	socketReady = true;

	std::string universesStr;
	for (size_t i = 0; i < cfg.universes.size(); ++i) {
		universesStr += (i ? "," : "") + ofToString(cfg.universes[i]);
	}
	ofLogNotice("ArtnetSender") << "sending to " << cfg.ip << ":" << cfg.port
		<< ", universes [" << universesStr << "]"
		<< ", start_channel " << cfg.startChannel
		<< ", color_order " << cfg.colorOrder
		<< ", start_corner " << cfg.startCorner
		<< ", " << (cfg.snake ? "snake" : "row-by-row");
}

//--------------------------------------------------------------
void ArtnetSender::close() {
	if (socketReady) {
		udp.Close();
		socketReady = false;
	}
}

//--------------------------------------------------------------
glm::ivec2 ArtnetSender::mapIndex(int i, int gridW, int gridH) const {
	const int row = i / gridW; ///< Row counted from the start corner.
	const int col = i % gridW; ///< Position within that row, from the corner.
	// Snake mode runs every other row backwards; row-by-row keeps direction.
	const bool reversed = cfg.snake && (row % 2 == 1);
	const int c = reversed ? (gridW - 1 - col) : col;
	const int x = fromLeft ? c : (gridW - 1 - c);
	const int y = fromTop ? row : (gridH - 1 - row);
	return {x, y};
}

//--------------------------------------------------------------
void ArtnetSender::send(const ofPixels & grid) {
	if (!cfg.enabled || !socketReady || !grid.isAllocated()) {
		return;
	}

	const int gridW = grid.getWidth();
	const int gridH = grid.getHeight();
	const int lightCount = gridW * gridH;
	const int channelsPerLight = static_cast<int>(cfg.colorOrder.size());

	int totalChannels = (cfg.startChannel - 1) + lightCount * channelsPerLight;
	if (totalChannels > kMaxDmxChannels) {
		if (!warnedOverflow) {
			warnedOverflow = true;
			ofLogWarning("ArtnetSender") << "grid needs " << totalChannels
				<< " channels but a universe holds " << kMaxDmxChannels
				<< " - trailing lights are dropped";
		}
		totalChannels = kMaxDmxChannels;
	}
	// The ArtDMX data length must be even (protocol requirement).
	const int dmxLength = std::min(kMaxDmxChannels, totalChannels + (totalChannels % 2));

	dmxBuffer.assign(dmxLength, 0);
	for (int i = 0; i < lightCount; ++i) {
		const int base = (cfg.startChannel - 1) + i * channelsPerLight;
		if (base + channelsPerLight > dmxLength) {
			break;
		}
		const glm::ivec2 cell = mapIndex(i, gridW, gridH);
		const ofColor px = grid.getColor(cell.x, cell.y);
		for (int ch = 0; ch < channelsPerLight; ++ch) {
			std::uint8_t v = 0;
			switch (cfg.colorOrder[ch]) {
			case 'R': v = px.r; break;
			case 'G': v = px.g; break;
			case 'B': v = px.b; break;
			// Derived white for RGBW fixtures: the common component.
			case 'W': v = std::min({px.r, px.g, px.b}); break;
			}
			dmxBuffer[base + ch] = v;
		}
	}

	// One sequence number per frame, shared by all universes of that frame
	// so the node can align them. 0 means "sequencing disabled", skip it.
	++sequence;
	if (sequence == 0) {
		sequence = 1;
	}

	for (const int universe : cfg.universes) {
		sendArtDmx(universe, dmxBuffer);
	}
}

//--------------------------------------------------------------
void ArtnetSender::sendArtDmx(int universe, const std::vector<std::uint8_t> & dmx) {
	// ArtDMX packet layout (Art-Net 4 spec): 18-byte header + DMX data.
	std::vector<std::uint8_t> pkt(18 + dmx.size());
	std::memcpy(pkt.data(), "Art-Net\0", 8);
	pkt[8] = 0x00; // OpDmx = 0x5000, little-endian.
	pkt[9] = 0x50;
	pkt[10] = 0;  // Protocol version 14, big-endian.
	pkt[11] = 14;
	pkt[12] = sequence;
	pkt[13] = 0; // Physical input port (informational only).
	pkt[14] = static_cast<std::uint8_t>(universe & 0xFF);        // SubUni.
	pkt[15] = static_cast<std::uint8_t>((universe >> 8) & 0x7F); // Net.
	pkt[16] = static_cast<std::uint8_t>((dmx.size() >> 8) & 0xFF); // Length hi.
	pkt[17] = static_cast<std::uint8_t>(dmx.size() & 0xFF);        // Length lo.
	std::memcpy(pkt.data() + 18, dmx.data(), dmx.size());

	udp.Send(reinterpret_cast<const char *>(pkt.data()), static_cast<int>(pkt.size()));
	++packetCount;
}
