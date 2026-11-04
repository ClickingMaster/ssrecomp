#pragma once
// Keeps the sound continuous when the emulation runs slower than a DS (Black 2 on a Switch
// without the JIT): the samples of each frame are stretched to the time the frame really
// took, at their own pitch, instead of running out between frames (which crackles and
// stutters). The method is WSOLA: the output is made of overlapping pieces of the input,
// each taken from near where it ideally belongs, at the offset where it best continues the
// piece before it, and cross-faded with it.
//
// At full speed nothing is stretched: the samples pass through as they are, with no added
// delay. Stretching starts when the frames take clearly longer than a DS frame and ends,
// seamlessly, once they no longer do.

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

class AudioStretch
{
public:
    AudioStretch()
    {
        for (int i = 0; i < W; i++) Win[i] = 0.5f - 0.5f * std::cos(2.0f * 3.14159265f * i / W);
    }

    // in: n stereo samples (interleaved, 16-bit). slowdown: how much longer than a DS frame
    // the frames are taking (1 at full speed), with the queue correction already in it.
    // The result is appended to out.
    void Process(const int16_t* in, int n, double slowdown, std::vector<int16_t>& out)
    {
        if (!Active)
        {
            if (slowdown < StartAt)
            {
                out.insert(out.end(), in, in + n * 2);
                return;
            }
            // Start: the first piece continues exactly where the plain samples ended.
            Active = true;
            Buf.clear();
            Prev = -H;
            Pos = 0;
            CalmFrames = 0;
        }
        Buf.insert(Buf.end(), in, in + n * 2);
        const double ratio = std::min(std::max(slowdown, 1.0), 3.0);
        CalmFrames = slowdown < StopAt ? CalmFrames + 1 : 0;
        Run(ratio, out);
        if (CalmFrames >= 30)
        {
            // Back to full speed for half a second: the rest of the input as it is, then
            // straight through again. (After the last piece the input continues at Prev + H.)
            const int from = Prev + H;
            if (from >= 0 && from * 2 < (int)Buf.size()) out.insert(out.end(), Buf.begin() + from * 2, Buf.end());
            Buf.clear();
            Active = false;
        }
    }

    // A speed/timeline change must not carry pieces of the previous stream into the new one.
    void Reset()
    {
        Buf.clear();
        Prev = 0;
        Pos = 0;
        Active = false;
        CalmFrames = 0;
    }

    bool Stretching() const { return Active; }

private:
    static constexpr int W = 1024;       // piece length (21 ms at 48 kHz)
    static constexpr int H = W / 2;      // output step: pieces overlap by half
    static constexpr int D = 384;        // how far a piece may move from its ideal place
    static constexpr double StartAt = 1.08, StopAt = 1.03;

    float Win[W];
    std::vector<int16_t> Buf; // input not used up yet (stereo, interleaved)
    int Prev = 0;             // where the last piece started, in Buf
    double Pos = 0;           // where the next piece ideally starts, in Buf
    bool Active = false;
    int CalmFrames = 0;

    int Samples() const { return (int)Buf.size() / 2; }
    float Mono(int i) const { return (float)Buf[i * 2] + (float)Buf[i * 2 + 1]; }

    void Run(double ratio, std::vector<int16_t>& out)
    {
        for (;;)
        {
            const int ideal = (int)std::lround(Pos);
            // Everything a step can look at must be there: the candidates and the rest of
            // the previous piece.
            if (ideal + D + W > Samples() || Prev + W > Samples()) break;
            const int next = Prev + H; // where the previous piece's sound naturally goes on
            const int best = Search(next, std::max(0, ideal - D), ideal + D);
            for (int i = 0; i < H; i++)
            {
                const float wa = Win[H + i], wb = Win[i];
                for (int c = 0; c < 2; c++)
                {
                    const float a = next + i >= 0 ? Buf[(next + i) * 2 + c] : 0.0f;
                    const float v = a * wa + Buf[(best + i) * 2 + c] * wb;
                    out.push_back((int16_t)std::max(-32768.0f, std::min(32767.0f, std::nearbyint(v))));
                }
            }
            Prev = best;
            Pos += H / ratio;
            // Drop the input no later step can reach.
            const int keep = std::min(Prev, (int)std::lround(Pos) - D) - 1;
            if (keep > 4096)
            {
                Buf.erase(Buf.begin(), Buf.begin() + keep * 2);
                Prev -= keep;
                Pos -= keep;
            }
        }
    }

    // The start in [lo, hi] whose first H samples are most like the samples at `next` (the
    // natural continuation), by normalised correlation of the two channels summed: first
    // every 4th start on every 4th sample, then the starts around the best one on every
    // other sample. The natural continuation itself wins unless something is clearly
    // better: at full speed that reproduces the input exactly.
    int Search(int next, int lo, int hi) const
    {
        if (next < 0) return lo <= 0 && 0 <= hi ? 0 : lo;
        float ref[H / 2];
        for (int i = 0; i < H / 2; i++) ref[i] = Mono(next + i * 2);
        auto score = [&](int p, int step) {
            float c = 0, e = 1e-3f, re = 1e-3f;
            for (int i = 0; i < H / 2; i += step / 2)
            {
                const float v = Mono(p + i * 2);
                c += v * ref[i];
                e += v * v;
                re += ref[i] * ref[i];
            }
            return c / std::sqrt(e * re);
        };
        int coarse = lo;
        float coarseScore = -2;
        for (int p = lo; p <= hi; p += 4)
        {
            const float sc = score(p, 4);
            if (sc > coarseScore)
            {
                coarseScore = sc;
                coarse = p;
            }
        }
        int best = next >= lo && next <= hi ? next : coarse;
        float bestScore = score(best, 2) + 0.02f;
        for (int p = std::max(lo, coarse - 3); p <= std::min(hi, coarse + 3); p++)
        {
            const float sc = score(p, 2);
            if (sc > bestScore)
            {
                bestScore = sc;
                best = p;
            }
        }
        return best;
    }
};

// Queue feedback must also be able to START stretching. Applying it only once stretching
// is active leaves a full-speed stream unable to recover from a short or empty queue.
inline double AudioQueueSlowdown(double frameSlowdown, int queuedSamples)
{
    constexpr double target = 4800.0; // 100 ms at 48 kHz
    return frameSlowdown * (1.0 + 0.5 * (target - std::max(0, queuedSamples)) / target);
}
