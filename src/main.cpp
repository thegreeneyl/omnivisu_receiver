#include "ofMain.h"
#include "ofApp.h"

//========================================================================
int main() {
	ofGLFWWindowSettings settings;
	settings.setGLVersion(3, 3);
	// Default canvas: two eyes side-by-side. ofApp::setup() applies the final
	// size/fullscreen/vsync from config.json once the data path is available.
	settings.setSize(828, 280);
	settings.windowMode = OF_WINDOW;

	auto window = ofCreateWindow(settings);

	ofRunApp(window, std::make_shared<ofApp>());
	ofRunMainLoop();
}
