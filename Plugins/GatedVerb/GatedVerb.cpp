// SPDX-License-Identifier: AGPL-3.0-only
// Copyright (C) 2026 Jerod Ulicki

// Gated Verb (SQ: Gated Verb) - 1980s non-linear reverb. Instead of a natural decay the
// reverb's impulse response follows a drawn envelope: PRE-DELAY, ATTACK, HOLD, RELEASE
// (attack + hold + release up to 500 ms), then stops dead.
//   CLASSIC  - the classic non-linear/gated programme
//   PANNER   - the reverb sweeps from left to right across the envelope
//   POWERBOX - denser, punchier, with low-mid weight and a little drive
// Built from sparse "velvet noise" reflections shaped by the envelope; DIFFUSION runs from
// clear (sparse) to rich (dense). Output MONO / STEREO / WIDE. LO CUT 24 dB/oct, HI CUT 12.

#include "Fx.h"

using namespace ltp;

class GatedVerb : public FxBase
{
public:
    GatedVerb() : FxBase (createLayout()) {}

    static Layout createLayout()
    {
        using U = param::Unit;
        Layout l;
        addCommon (l);
        param::addChoice (l, "model", "Model", { "Classic", "Panner", "Powerbox" }, 0);
        param::addFloat (l, "loCut", "Lo Cut", 20.0f, 6000.0f, 60.0f, U::Hz, 400.0f);
        param::addFloat (l, "hiCut", "Hi Cut", 400.0f, 20000.0f, 12000.0f, U::Hz, 5000.0f);
        param::addFloat (l, "predelay", "Pre-delay", 0.0f, 170.0f, 0.0f, U::Ms, 40.0f, 0.1f);
        param::addFloat (l, "attack", "Attack", 0.0f, 500.0f, 10.0f, U::Ms, 60.0f, 0.1f);
        param::addFloat (l, "hold", "Hold", 0.0f, 500.0f, 180.0f, U::Ms, 150.0f, 0.1f);
        param::addFloat (l, "release", "Release", 0.0f, 500.0f, 90.0f, U::Ms, 100.0f, 0.1f);
        param::addChoice (l, "width", "Output", { "Mono", "Stereo", "Wide" }, 1);
        param::addFloat (l, "diffusion", "Diffusion", 0.0f, 100.0f, 70.0f, U::Percent);
        addFxCommon (l);
        return l;
    }

    Info getInfo() const override
    {
        Info i; LTP_BASE_INFO (i)
        i.dynamics = false;
        i.editorWidth = 820;
        return i;
    }

    std::vector<Section> getSections() const override
    {
        return {
            { "GATE ENVELOPE", 2, { segment ("model", "MODEL"),
                                    knob ("predelay", "PRE-DELAY"),
                                    knob ("attack", "ATTACK"), knob ("hold", "HOLD"), knob ("release", "RELEASE", "Attack + hold + release is limited to 500 ms (scaled to fit)") } },
            { "TONE & SPACE", 1, { knob ("loCut", "LO CUT", "24 dB/oct"), knob ("hiCut", "HI CUT", "12 dB/oct"),
                                   knob ("diffusion", "DIFFUSION", "Min: clear and sparse. Max: rich and dense"),
                                   segment ("width", "OUTPUT") }, {}, true },
            outputSection()
        };
    }

    Faceplate getFaceplate() const override
    {
        Faceplate f;
        f.title = "GATED REVERB"; f.subtitle = "Non-linear reverb  -  SQ model: Gated Verb";
        f.panel = 0xff1b1b1d; f.ink = 0xffe9e9e9; f.knobs = Faceplate::BlackSkirt; f.led = 0xffff4a3a;
        f.rows = { { fpDisplay ("hold", "HOLD"), fpButtons ("model", "PROGRAM"), fpKnob ("predelay", "PRE-DELAY", 1), fpKnob ("attack", "ATTACK", 1),
                     fpKnob ("hold", "HOLD", 1), fpKnob ("release", "RELEASE", 1) },
                   { fpKnob ("loCut", "LO CUT", 0), fpKnob ("hiCut", "HI CUT", 0), fpKnob ("diffusion", "DIFFUSION", 1), fpButtons ("width", "OUTPUT") } };
        return f;
    }

    double tailSeconds() const override { return 0.75; }

    static constexpr int maxTaps = 900;

protected:
    void prepareFx (double sampleRate, int) override
    {
        input.prepare ((int) (sampleRate * 0.72));
        for (auto& c : diff) for (auto& a : c) a.prepare ((int) (sampleRate * 0.01));
        const float s = (float) sampleRate / 1000.0f;
        static constexpr float apMs[2][2] = { { 1.7f, 4.3f }, { 2.3f, 3.9f } };
        for (int c = 0; c < 2; ++c) for (int k = 0; k < 2; ++k) diff[c][k].delay = apMs[c][k] * s;
        lastKey = -1.0f;
        body[0].set (Biquad::Peak, sampleRate, 220.0f, 0.8f, 4.0f);
        body[1] = body[0];
    }

    void resetFx() override
    {
        input.reset(); loCut.reset();
        for (auto& h : hiCut) h.reset();
        for (auto& c : diff) for (auto& a : c) a.reset();
        for (auto& b : body) b.reset();
    }

    void beginBlock() override
    {
        const float lc = pv ("loCut"), hc = pv ("hiCut");
        if (std::abs (lc - lastLc) > 0.01f) { lastLc = lc; loCut.set (Biquad::HighPass, sr, lc); }
        if (std::abs (hc - lastHc) > 0.01f) { lastHc = hc; for (auto& h : hiCut) h.set (Biquad::LowPass, sr, hc, 0.707f); }
        loCut.enabled = lc > 21.0f;
        hiOn = hc < 19900.0f;
        powerbox = pi ("model") == 2;
        widthMode = pi ("width");
        const float g = pv ("diffusion") / 100.0f * 0.55f;
        for (auto& c : diff) for (auto& a : c) a.g = g;

        const float key = pv ("predelay") + pv ("attack") * 1.1f + pv ("hold") * 1.3f + pv ("release") * 1.7f
                        + pv ("diffusion") * 0.37f + (float) pi ("model") * 5000.0f + (float) widthMode * 20000.0f;
        if (std::abs (key - lastKey) > 1.0e-3f) { lastKey = key; buildTaps(); }
    }

    void buildTaps()
    {
        float a = pv ("attack"), h = pv ("hold"), r = pv ("release");
        const float total = a + h + r;
        if (total > 500.0f) { const float k = 500.0f / total; a *= k; h *= k; r *= k; }
        const float len = std::max (a + h + r, 5.0f);
        const int model = pi ("model");
        float density = 300.0f + 1500.0f * pv ("diffusion") / 100.0f;   // taps per second
        if (model == 2) density *= 1.5f;
        const float pre = pv ("predelay");
        const float s = (float) sr / 1000.0f;

        auto env = [&] (float t)   // t in ms from the start of the envelope
        {
            if (t < a) return a > 0.0f ? t / a : 1.0f;
            if (t < a + h) return 1.0f;
            const float x = (t - a - h) / std::max (r, 0.001f);
            return model == 2 ? 1.0f - x * x * x : 1.0f - x;   // powerbox holds its energy longer
        };

        for (int c = 0; c < 2; ++c)
        {
            Noise rng; rng.seed (c == 0 || widthMode == 0 ? 1001u : 2002u);
            const float cell = 1000.0f / density;   // ms per tap
            int count = 0; float energy = 0.0f;
            for (float t0 = 0.0f; t0 < len && count < maxTaps; t0 += cell)
            {
                const float t = t0 + cell * rng.uniform();
                if (t >= len) break;
                float gain = env (t) * (rng.next() < 0.0f ? -1.0f : 1.0f);
                if (model == 1)   // panner: sweeps left -> right over the envelope
                {
                    const float th = 1.5707963f * t / len;
                    gain *= c == 0 ? std::cos (th) : std::sin (th);
                }
                tapPos[c][count] = (pre + t) * s;
                tapGain[c][count] = gain;
                energy += gain * gain;
                ++count;
            }
            tapCount[c] = count;
            const float n = 0.8f / std::sqrt (std::max (energy, 1.0e-3f) * (model == 1 ? 2.0f : 1.0f));
            for (int k = 0; k < count; ++k) tapGain[c][k] *= n;
        }
    }

    void processFrame (float inL, float inR, float& wetL, float& wetR) override
    {
        float x = loCut.process (0.5f * (inL + inR));
        if (hiOn) x = hiCut[0].process (x);
        input.push (x);

        float out[2];
        for (int c = 0; c < 2; ++c)
        {
            float acc = 0.0f;
            const int n = tapCount[c];
            for (int k = 0; k < n; ++k) acc += input.tap ((int) tapPos[c][k]) * tapGain[c][k];
            for (auto& a : diff[c]) acc = a.process (acc);
            if (powerbox) acc = softSat (body[c].process (acc), 0.35f);
            out[c] = acc;
        }
        if (widthMode == 0) out[0] = out[1] = 0.5f * (out[0] + out[1]);
        if (widthMode == 2)
        {
            const float m = 0.5f * (out[0] + out[1]), sd = 0.5f * (out[0] - out[1]) * 1.6f;
            out[0] = m + sd; out[1] = m - sd;
        }
        wetL = out[0]; wetR = out[1];
    }

private:
    DelayLine input;
    Filter24 loCut;
    Biquad hiCut[2], body[2];
    AllpassDiffuser diff[2][2];
    float tapPos[2][maxTaps] {}, tapGain[2][maxTaps] {};
    int tapCount[2] {}, widthMode = 1;
    float lastKey = -1, lastLc = -1, lastHc = -1;
    bool hiOn = true, powerbox = false;
};

LTP_REGISTER (GatedVerb)
