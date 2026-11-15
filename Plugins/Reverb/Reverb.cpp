// SPDX-License-Identifier: AGPL-3.0-only
// Copyright (C) 2026 Jerod Ulicki

// Reverb (SQ: SMR Reverb) - algorithmic reverb with the SMR's four models and expert pages.
//   CLASSIC - vintage digital hall: medium size, slightly darker, more movement
//   HALL    - large concert hall: long, even build-up
//   ROOM    - small/medium room: strong early reflections, short dense tail
//   EMT     - plate: no early reflections, very fast dense build-up, bright
// Signal path: LF/HF cut (24 dB/oct) -> pre-delay -> source diffusion -> early reflections
// (size, shape, detail) + two discrete echoes -> 8-line feedback delay network with in-loop
// body diffusion, modulation, three-band decay (LF multiplier / mid / HF onset + slope)
// -> tail diffusion -> colour EQ -> reflection and decay levels.

#include "Fx.h"

using namespace ltp;

namespace
{
    juce::String offOrHz (float v, bool lowIsOff)
    {
        if (lowIsOff ? v < 1.0f : v > 19900.0f) return "Off";
        return param::format (v, param::Unit::Hz);
    }
    juce::String times (float v) { return juce::String (v, 2) + "x"; }
}

class Reverb : public FxBase
{
public:
    Reverb() : FxBase (createLayout()) {}

    static Layout createLayout()
    {
        using U = param::Unit;
        Layout l;
        addCommon (l);
        param::addChoice (l, "model", "Model", { "Classic", "Hall", "Room", "EMT" }, 1);
        param::addFloatText (l, "lfCut", "LF Cut", 0.0f, 400.0f, 0.0f, [] (float v) { return offOrHz (v, true); }, 100.0f);
        param::addFloatText (l, "hfCut", "HF Cut", 2000.0f, 20000.0f, 20000.0f, [] (float v) { return offOrHz (v, false); }, 8000.0f);
        param::addFloat (l, "predelay", "Pre-delay", 0.0f, 500.0f, 20.0f, U::Ms, 60.0f, 0.1f);
        param::addFloat (l, "decay", "Decay Time", 0.2f, 20.0f, 2.2f, U::Seconds, 2.5f, 0.01f);
        param::addFloat (l, "hfDecay", "HF Decay", 1000.0f, 20000.0f, 4500.0f, U::Hz, 5000.0f);
        param::addFloat (l, "hfSlope", "HF Slope", 0.0f, 100.0f, 50.0f, U::Percent);
        param::addFloat (l, "diffusion", "Source Diffusion", 0.0f, 100.0f, 60.0f, U::Percent);
        param::addFloat (l, "size", "Size", 0.0f, 100.0f, 50.0f, U::Percent);
        param::addFloat (l, "shape", "Shape", 0.0f, 100.0f, 50.0f, U::Percent);
        param::addFloat (l, "detail", "Ref Detail", 0.0f, 100.0f, 50.0f, U::Percent);
        param::addFloat (l, "echo1", "Echo 1 (L)", 0.0f, 1000.0f, 120.0f, U::Ms, 200.0f, 0.1f);
        param::addFloat (l, "echo1Lvl", "Echo 1 Level", -40.0f, 0.0f, -40.0f, U::Db, -1.0f, 0.1f);
        param::addFloat (l, "echo2", "Echo 2 (R)", 0.0f, 1000.0f, 170.0f, U::Ms, 200.0f, 0.1f);
        param::addFloat (l, "echo2Lvl", "Echo 2 Level", -40.0f, 0.0f, -40.0f, U::Db, -1.0f, 0.1f);
        param::addFloat (l, "bodyDiff", "Body Diffusion", 0.0f, 100.0f, 60.0f, U::Percent);
        param::addFloat (l, "tailDiff", "Tail Diffusion", 0.0f, 100.0f, 50.0f, U::Percent);
        param::addFloat (l, "modDepth", "Mod Depth", 0.0f, 100.0f, 30.0f, U::Percent);
        param::addFloat (l, "modSpeed", "Mod Speed", 0.05f, 5.0f, 0.6f, U::Hz, 0.8f);
        param::addFloatText (l, "lfDecay", "LF Decay", 0.5f, 2.0f, 1.0f, times, 1.0f);
        param::addFloat (l, "lfXover", "LF Xover", 50.0f, 1000.0f, 250.0f, U::Hz, 250.0f);
        param::addFloat (l, "colour", "Colour", -12.0f, 12.0f, 0.0f, U::Db, -1.0f, 0.1f);
        param::addFloat (l, "colourFreq", "Colour Freq", 100.0f, 10000.0f, 2000.0f, U::Hz, 1500.0f);
        param::addFloat (l, "reflLevel", "Reflection Level", -40.0f, 6.0f, 0.0f, U::Db, -1.0f, 0.1f);
        param::addFloat (l, "decayLevel", "Decay Level", -40.0f, 6.0f, 0.0f, U::Db, -1.0f, 0.1f);
        addFxCommon (l);
        return l;
    }

    Info getInfo() const override
    {
        Info i; LTP_BASE_INFO (i)
        i.dynamics = false;
        i.editorWidth = 960;
        return i;
    }

    std::vector<Section> getSections() const override
    {
        return {
            { "REVERB", 1, { segment ("model", "MODEL"),
                             knob ("predelay", "PRE-DELAY"), knob ("decay", "DECAY (RT60)"),
                             knob ("lfCut", "LF CUT", "24 dB/oct high-pass on the reverb input"),
                             knob ("hfCut", "HF CUT", "24 dB/oct low-pass on the reverb input"),
                             knob ("hfDecay", "HF DECAY", "Frequency above which the tail decays faster"),
                             knob ("hfSlope", "HF SLOPE", "How much faster the high frequencies decay") } },
            { "REFLECTIONS", 4, { knob ("diffusion", "SOURCE DIFF"), knob ("size", "SIZE"),
                                  knob ("shape", "SHAPE"), knob ("detail", "REF DETAIL") } },
            { "ECHOES", 2, { knob ("echo1", "ECHO 1 (L)"), knob ("echo1Lvl", "LEVEL 1"),
                             knob ("echo2", "ECHO 2 (R)"), knob ("echo2Lvl", "LEVEL 2") } },
            { "DECAY TEXTURE", 3, { knob ("bodyDiff", "BODY DIFF"), knob ("tailDiff", "TAIL DIFF", "Too much diffusion can sound metallic"),
                                    knob ("modDepth", "MOD DEPTH", "Adds density and chorusing - great on drums, less so on piano and vocals"),
                                    knob ("modSpeed", "MOD SPEED") } },
            { "DECAY SPECTRUM", 5, { knob ("lfDecay", "LF DECAY", "Low-frequency decay time as a multiple of the main decay"),
                                     knob ("lfXover", "LF XOVER"), knob ("colour", "COLOUR"), knob ("colourFreq", "COLOUR FREQ") } },
            { "LEVELS", 0, { knob ("reflLevel", "REFLECTIONS"), knob ("decayLevel", "DECAY") } },
            outputSection()
        };
    }

    Faceplate getFaceplate() const override
    {
        Faceplate f;
        f.title = "SPATIAL REVERB"; f.subtitle = "Algorithmic reverb  -  SQ model: SMR Reverb";
        f.panel = 0xff1d2733; f.ink = 0xffd8e6f2; f.finish = Faceplate::Brushed; f.knobs = Faceplate::Silver; f.led = 0xff35d0ff;
        f.rows = { { fpDisplay ("decay", "DECAY"), fpButtons ("model", "MODEL"), fpKnob ("predelay", "PRE-DELAY", 1), fpKnob ("decay", "DECAY", 2),
                     fpKnob ("hfDecay", "HF DECAY", 1), fpKnob ("hfSlope", "HF SLOPE", 1) },
                   { fpKnob ("lfCut", "LF CUT", 0), fpKnob ("hfCut", "HF CUT", 0), fpGap(), fpKnob ("diffusion", "SOURCE DIFF", 0), fpKnob ("size", "SIZE", 0),
                     fpKnob ("shape", "SHAPE", 0), fpKnob ("detail", "REF DETAIL", 0), fpGap(), fpKnob ("echo1", "ECHO 1", 0), fpKnob ("echo1Lvl", "LEVEL 1", 0),
                     fpKnob ("echo2", "ECHO 2", 0), fpKnob ("echo2Lvl", "LEVEL 2", 0) },
                   { fpKnob ("bodyDiff", "BODY DIFF", 0), fpKnob ("tailDiff", "TAIL DIFF", 0), fpKnob ("modDepth", "MOD DEPTH", 0), fpKnob ("modSpeed", "MOD SPEED", 0),
                     fpGap(), fpKnob ("lfDecay", "LF DECAY", 0), fpKnob ("lfXover", "LF XOVER", 0), fpKnob ("colour", "COLOUR", 0), fpKnob ("colourFreq", "COLOUR FREQ", 0),
                     fpGap(), fpKnob ("reflLevel", "REFLECTIONS", 0), fpKnob ("decayLevel", "DECAY LEVEL", 0) } };
        return f;
    }

    double tailSeconds() const override { return pv ("decay") * 1.6 + pv ("predelay") * 0.001 + 1.0; }

    static constexpr int N = 8;

protected:
    struct ModelData { float lines[N]; float ap[N]; float erStart, erLen; float erGain; float modMul; float bodyBoost; };
    static const ModelData& model (int m)
    {
        static const ModelData d[4] = {
            { { 29.7f, 37.1f, 41.1f, 43.7f, 53.3f, 59.9f, 67.9f, 73.3f }, { 3.1f, 4.3f, 5.3f, 6.1f, 7.1f, 7.9f, 8.9f, 9.7f }, 8.0f, 80.0f, 0.8f, 1.4f, 0.0f },   // Classic
            { { 41.1f, 47.3f, 53.9f, 61.7f, 69.3f, 77.9f, 85.1f, 93.7f }, { 3.7f, 4.9f, 5.9f, 6.7f, 7.7f, 8.9f, 9.7f, 11.3f }, 12.0f, 110.0f, 0.9f, 1.0f, 0.0f }, // Hall
            { { 13.1f, 17.9f, 19.7f, 23.3f, 27.1f, 29.9f, 31.7f, 37.3f }, { 1.9f, 2.3f, 2.9f, 3.3f, 3.7f, 4.1f, 4.7f, 5.3f }, 4.0f, 55.0f, 1.0f, 0.7f, 0.0f },     // Room
            { { 9.7f, 13.1f, 16.9f, 19.3f, 23.9f, 27.1f, 31.7f, 37.3f }, { 1.3f, 1.9f, 2.3f, 2.9f, 3.1f, 3.7f, 4.3f, 4.9f }, 0.0f, 0.0f, 0.0f, 0.6f, 0.15f } };   // EMT plate
        return d[juce::jlimit (0, 3, m)];
    }

    void prepareFx (double sampleRate, int) override
    {
        const float s = (float) sampleRate / 1000.0f;
        input.prepare ((int) (1600.0f * s));
        for (int i = 0; i < N; ++i)
        {
            lines[i].prepare ((int) (160.0f * s));
            bodyAp[i].prepare ((int) (20.0f * s));
            mod[i].reset ((float) i / (float) N);
        }
        static constexpr float srcMs[4] = { 2.97f, 2.23f, 7.9f, 5.77f };
        for (int i = 0; i < 4; ++i) { srcAp[i].prepare ((int) (10.0f * s)); srcAp[i].delay = srcMs[i] * s; }
        static constexpr float tailMs[2][2] = { { 3.31f, 6.17f }, { 3.77f, 5.53f } };
        for (int c = 0; c < 2; ++c) for (int k = 0; k < 2; ++k) { tailAp[c][k].prepare ((int) (10.0f * s)); tailAp[c][k].delay = tailMs[c][k] * s; }
        lastModel = -1;
        lastKey = -1.0f;
    }

    void resetFx() override
    {
        input.reset();
        for (int i = 0; i < N; ++i) { lines[i].reset(); bodyAp[i].reset(); lo[i].reset(); hi[i].reset(); }
        for (auto& a : srcAp) a.reset();
        for (auto& c : tailAp) for (auto& a : c) a.reset();
        lfCut.reset(); hfCut.reset();
        for (auto& c : colour) c.reset();
    }

    void beginBlock() override
    {
        const int m = pi ("model");
        const auto& md = model (m);
        const float s = (float) sr / 1000.0f;
        const float size = 0.5f + pv ("size") / 100.0f;
        const float rt = pv ("decay") * 1.08f;   // calibrated: interpolation + modulation losses shorten the tail ~7%
        const float rtLo = rt * pv ("lfDecay");
        const float rtHi = std::max (0.05f, rt * (1.0f - 0.92f * pv ("hfSlope") / 100.0f));

        for (int i = 0; i < N; ++i)
        {
            lineLen[i] = md.lines[i] * size * s;
            bodyAp[i].delay = md.ap[i] * s;
            const float loopSamples = lineLen[i] + bodyAp[i].delay;
            auto g = [&] (float t) { return std::min (0.995f, std::pow (10.0f, -3.0f * loopSamples / (t * (float) sr))); };
            gMid[i] = g (rt); gLo[i] = g (rtLo); gHi[i] = g (rtHi);
            lo[i].set (sr, pv ("lfXover"));
            hi[i].set (sr, pv ("hfDecay"));
        }

        const float body = clampf (pv ("bodyDiff") / 100.0f * 0.6f + md.bodyBoost, 0.0f, 0.75f);
        for (auto& a : bodyAp) a.g = body;
        float src = pv ("diffusion") / 100.0f * 0.7f;
        if (m == 3) src = std::max (src, 0.6f);
        for (auto& a : srcAp) a.g = src;
        const float tail = pv ("tailDiff") / 100.0f * 0.6f;
        for (auto& c : tailAp) for (auto& a : c) a.g = tail;

        modDepthSamples = pv ("modDepth") / 100.0f * 0.55f * s * md.modMul;
        for (auto& l : mod) l.setRate (sr, pv ("modSpeed") * (1.0f + 0.07f * (float) (&l - mod)));

        const float lc = pv ("lfCut"), hc = pv ("hfCut");
        lfCut.enabled = lc >= 1.0f; hfCut.enabled = hc <= 19900.0f;
        if (std::abs (lc - lastLc) > 0.01f) { lastLc = lc; lfCut.set (Biquad::HighPass, sr, std::max (lc, 10.0f)); }
        if (std::abs (hc - lastHc) > 0.01f) { lastHc = hc; hfCut.set (Biquad::LowPass, sr, hc); }
        const float cg = pv ("colour"), cf = pv ("colourFreq");
        colourOn = std::abs (cg) > 0.05f;
        if (std::abs (cg - lastCg) > 0.01f || std::abs (cf - lastCf) > 0.01f) { lastCg = cg; lastCf = cf; for (auto& c : colour) c.set (Biquad::Peak, sr, cf, 0.7f, cg); }

        predelay = pv ("predelay") * s;
        echo1 = pv ("echo1") * s; echo2 = pv ("echo2") * s;
        echo1Gain = pv ("echo1Lvl") <= -39.9f ? 0.0f : dbToGain (pv ("echo1Lvl"));
        echo2Gain = pv ("echo2Lvl") <= -39.9f ? 0.0f : dbToGain (pv ("echo2Lvl"));
        reflGain = dbToGain (pv ("reflLevel")) * md.erGain;
        decayGain = dbToGain (pv ("decayLevel"));

        // early reflection pattern (rebuilt only when it changes)
        const float key = (float) m * 1000.0f + pv ("size") * 7.0f + pv ("shape") * 0.31f + pv ("detail") * 0.0137f + (float) sr * 1.0e-6f;
        if (std::abs (key - lastKey) > 1.0e-4f)
        {
            lastKey = key;
            buildReflections (md, size);
        }
    }

    void buildReflections (const ModelData& md, float size)
    {
        erCount = 0;
        if (md.erLen <= 0.0f) return;
        const int count = 6 + (int) std::lround (pv ("detail") / 100.0f * 18.0f);   // 6..24 taps
        const float expo = 0.5f + 1.5f * pv ("shape") / 100.0f;                     // early-heavy .. late-heavy
        Noise rng; rng.seed (991u + (uint32_t) count);
        float norm = 0.0f;
        for (int k = 0; k < count && k < 24; ++k)
        {
            const float pos = std::pow (((float) k + 0.5f + 0.4f * rng.next()) / (float) count, expo);
            const float ms = md.erStart * size + pos * md.erLen * size;
            erDelay[k] = ms * (float) sr / 1000.0f;
            const float gain = (1.0f - 0.75f * pos) * (rng.next() < 0.0f ? -1.0f : 1.0f);
            const float pan = 0.5f + 0.45f * (k % 2 == 0 ? 1.0f : -1.0f) * (0.4f + 0.6f * rng.uniform());
            erGainL[k] = gain * std::sqrt (1.0f - pan);
            erGainR[k] = gain * std::sqrt (pan);
            norm += gain * gain;
            ++erCount;
        }
        const float n = 0.9f / std::sqrt (std::max (norm, 1.0f));
        for (int k = 0; k < erCount; ++k) { erGainL[k] *= n; erGainR[k] *= n; }
    }

    static void hadamard (float* x)
    {
        for (int h = 1; h < N; h <<= 1)
            for (int i = 0; i < N; i += h * 2)
                for (int j = i; j < i + h; ++j)
                {
                    const float a = x[j], b = x[j + h];
                    x[j] = a + b; x[j + h] = a - b;
                }
        const float s = 0.35355339f;   // 1/sqrt(8)
        for (int i = 0; i < N; ++i) x[i] *= s;
    }

    void processFrame (float inL, float inR, float& wetL, float& wetR) override
    {
        float x = 0.5f * (inL + inR);
        x = hfCut.process (lfCut.process (x));
        input.push (x);

        // pre-delayed, diffused source
        float src = input.read (predelay);
        for (auto& a : srcAp) src = a.process (src);

        // early reflections and echoes
        float erL = 0.0f, erR = 0.0f;
        for (int k = 0; k < erCount; ++k)
        {
            const float t = input.readLinear (predelay + erDelay[k]);
            erL += t * erGainL[k];
            erR += t * erGainR[k];
        }
        const float e1 = echo1Gain > 0.0f ? input.read (predelay + echo1) * echo1Gain : 0.0f;
        const float e2 = echo2Gain > 0.0f ? input.read (predelay + echo2) * echo2Gain : 0.0f;

        // FDN
        float v[N];
        for (int i = 0; i < N; ++i)
        {
            mod[i].advance();
            const float d = lineLen[i] + modDepthSamples * mod[i].value (Lfo::Sine);
            float y = lines[i].read (std::max (1.0f, d) - 1.0f);
            y = bodyAp[i].process (y);
            // three-band decay: low shelf (LF multiplier) then high shelf (HF onset/slope)
            float a = y + (gLo[i] / gMid[i] - 1.0f) * lo[i].lp (y);
            a = a + (gHi[i] / gMid[i] - 1.0f) * hi[i].hp (a);
            v[i] = a * gMid[i];
        }
        float outL = 0.0f, outR = 0.0f;
        static constexpr float sl[N] = { 1, -1, 1, 1, -1, 1, -1, -1 };
        static constexpr float sR[N] = { 1, 1, -1, 1, 1, -1, -1, 1 };
        for (int i = 0; i < N; ++i) { outL += v[i] * sl[i]; outR += v[i] * sR[i]; }
        hadamard (v);
        const float inject = src + 0.3f * (e1 + e2) + 0.35f * (erL + erR);
        static constexpr float inSign[N] = { 1, 1, -1, 1, -1, -1, 1, -1 };
        for (int i = 0; i < N; ++i) lines[i].push (v[i] + inject * inSign[i] * 0.5f);

        outL *= 0.35f; outR *= 0.35f;
        for (auto& a : tailAp[0]) outL = a.process (outL);
        for (auto& a : tailAp[1]) outR = a.process (outR);

        wetL = outL * decayGain + erL * reflGain + e1;
        wetR = outR * decayGain + erR * reflGain + e2;
        if (colourOn) { wetL = colour[0].process (wetL); wetR = colour[1].process (wetR); }
        else { colour[0].process (wetL); colour[1].process (wetR); }
    }

private:
    DelayLine input, lines[N];
    AllpassDiffuser bodyAp[N], srcAp[4], tailAp[2][2];
    OnePole lo[N], hi[N];
    Lfo mod[N];
    Filter24 lfCut, hfCut;
    Biquad colour[2];
    float lineLen[N] {}, gMid[N] {}, gLo[N] {}, gHi[N] {};
    float erDelay[24] {}, erGainL[24] {}, erGainR[24] {};
    int erCount = 0, lastModel = -1;
    float lastKey = -1, lastLc = -1, lastHc = -1, lastCg = -100, lastCf = -1;
    float modDepthSamples = 0, predelay = 0, echo1 = 0, echo2 = 0, echo1Gain = 0, echo2Gain = 0, reflGain = 1, decayGain = 1;
    bool colourOn = false;
};

LTP_REGISTER (Reverb)
