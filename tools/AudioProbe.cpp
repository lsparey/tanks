// Auditions the synthesized sound effects without launching the game:
// engine drone spooling idle -> full throttle, a gun shot, then a near and a
// far explosion. Also the quickest way to check the machine actually exposes
// a usable playback device (exit code 1 if not).
#include <chrono>
#include <iostream>
#include <thread>

#include "../src/audio/AudioEngine.h"

int main() {
    AudioEngine audio;
    audio.setEnabled(true);
    if (!audio.enabled()) {
        std::cerr << "Audio device unavailable\n";
        return 1;
    }
    using namespace std::chrono_literals;

    std::cout << "Engine: idle, then spooling to full throttle\n";
    for (int frame = 0; frame <= 240; ++frame) {
        // First second holds idle, then ramps -- mimics per-frame calls from
        // Application::mainLoop.
        float intensity = std::min(1.0f, std::max(0.0f, (frame - 60) / 120.0f));
        audio.updateEngineSound(intensity, 1.0f / 60.0f);
        std::this_thread::sleep_for(16ms);
    }

    std::cout << "Shot\n";
    audio.playShot();
    std::this_thread::sleep_for(1200ms);

    std::cout << "Explosion (point blank)\n";
    audio.playExplosion(0.0f);
    std::this_thread::sleep_for(2500ms);

    std::cout << "Explosion (60 units away)\n";
    audio.playExplosion(60.0f);
    std::this_thread::sleep_for(2500ms);

    audio.setEnabled(false);
    return 0;
}
