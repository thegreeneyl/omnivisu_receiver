#pragma once

#include "ofMain.h"
#include "ofxNetwork.h"

#include <cstdint>
#include <string>
#include <vector>

/// Sends the fixture light grid as ArtNet (ArtDMX) datagrams to a node at a
/// fixed IP. The whole grid fits one universe's DMX frame; the SAME frame is
/// sent to every configured universe (the building repeats the lights on two
/// sides, each side addressed as its own universe).
///
/// Channel mapping: the traversal starts at a configurable corner of the grid
/// and moves horizontally first, then advances one row - either serpentine
/// ("snake": every other row runs backwards) or strictly row by row in the
/// same direction. Each light contributes len(color_order) channels in that
/// string's order (e.g. "RGB", "GBR"; "W" is derived as min(R,G,B) for future
/// RGBW fixtures). start_channel is the 1-based DMX address of the first
/// light; channels before it are sent as 0.
class ArtnetSender {
public:
	struct Config {
		bool enabled = false;
		std::string ip = "255.255.255.255"; ///< Node IP (or broadcast).
		int port = 6454;                    ///< ArtNet standard port.
		std::vector<int> universes{0};      ///< 15-bit port-addresses to send to.
		int startChannel = 1;               ///< 1-based DMX address of light 0.
		std::string colorOrder = "RGB";     ///< Channel order per light (R/G/B/W).
		std::string startCorner = "top_left"; ///< top_left/top_right/bottom_left/bottom_right.
		bool snake = true;                  ///< Serpentine rows vs. all rows same direction.
	};

	/// Applies the config and (re)creates the UDP socket. Safe to call again
	/// on a runtime config reload. Invalid color_order / start_corner values
	/// fall back to "RGB" / "top_left" with a warning.
	void setup(const Config & cfg);
	void close();

	bool isEnabled() const { return cfg.enabled && socketReady; }

	/// Builds the DMX frame from the light grid (one byte per channel, the
	/// traversal and color order from the config) and sends it to every
	/// configured universe. Call once per rendered frame.
	void send(const ofPixels & grid);

	std::uint64_t getPacketCount() const { return packetCount; }

private:
	/// Maps output light index i (0 = first light after start_channel) to the
	/// grid cell it samples, honoring start corner and snake mode.
	glm::ivec2 mapIndex(int i, int gridW, int gridH) const;
	void sendArtDmx(int universe, const std::vector<std::uint8_t> & dmx);

	Config cfg;
	bool socketReady = false;
	bool fromTop = true;
	bool fromLeft = true;
	bool warnedOverflow = false; ///< One-shot "grid exceeds 512 channels" warning.
	std::uint8_t sequence = 0;
	std::uint64_t packetCount = 0;
	ofxUDPManager udp;
	std::vector<std::uint8_t> dmxBuffer;
};
