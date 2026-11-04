#include "audiostretch.h"
#include <cassert>
#include <cstdio>
int main() {
    AudioStretch audio;
    std::vector<int16_t> old(1600, 12000), fresh(1600, -9000), output;
    for (int i = 0; i < 10; ++i) audio.Process(old.data(), 800, 1.5, output);
    assert(audio.Stretching());
    audio.Reset();
    assert(!audio.Stretching());
    output.clear();
    audio.Process(fresh.data(), 800, 1.0, output);
    assert(output == fresh); // no samples from the previous fast-forward timeline
    // An underfilled queue must engage recovery even at full emulation speed.
    audio.Reset(); output.clear();
    audio.Process(fresh.data(), 800, AudioQueueSlowdown(1.0, 0), output);
    assert(audio.Stretching());
    assert(AudioQueueSlowdown(1.0, 4800) == 1.0);
    assert(AudioQueueSlowdown(1.0, 0) > 1.08);
    assert(AudioQueueSlowdown(1.0, 6000) < 1.0);
    // Model a device clock running 1% faster, with a 1024-sample output period.
    // Queue feedback must replenish the reserve instead of recurring starvation.
    int queued = 2400, underruns = 0; double consumed = 0;
    audio.Reset();
    for (int frame = 0; frame < 600; ++frame) {
        consumed += 808.0;
        while (consumed >= 1024) {
            if (queued < 1024 && frame > 60) ++underruns;
            queued = std::max(0, queued - 1024); consumed -= 1024;
        }
        output.clear();
        audio.Process(fresh.data(), 800, AudioQueueSlowdown(1.0, queued), output);
        queued += (int)output.size() / 2;
        assert(queued < 12000);
    }
    assert(underruns == 0);
    assert(queued > 1024);
    puts("Audio recovery: clean timeline reset and full-speed queue replenishment passed");
}
