// SPDX-License-Identifier: AGPL-3.0-only
// Copyright (C) 2026 Jerod Ulicki

// BBD Delay (SQ: Bucket Brigade) - modeled on a bucket-brigade-chip analogue delay.
//  - A BBD's bandwidth falls as the delay gets longer (slower clock), so long repeats are
//    darker; each trip round the loop is filtered again. CLEAN removes the noise and grit
//    but keeps the band-limiting, like the SQ model.
//  - Compander noise that breathes with the signal, slight clock wander, and DIST, which
//    switches in a non-linear feedback path for driven, self-oscillating repeats.
//  - LOW DAMP / HIGH DAMP shape the feedback loop; slope sets how steep the damping is.

#include "Common/DelayTiming.h"

using namespace ltp;

class BbdDelay : public FxBase
{
public:
    BbdDelay() : FxBase (createLayout()) {}

    static Layout createLayout()
    {
        using U = param::Unit;
        Layout l;
        addCommon (l);
        delaytiming::addParams (l, 300.0f, 450.0f);
        delaytiming::addInputFilters (l);
        param::addFloat (l, "lowDamp", "Low Damp", 20.0f, 1000.0f, 60.0f, U::Hz, 150.0f);
        param::addFloat (l, "highDamp", "High Damp", 1000.0f, 20000.0f, 6000.0f, U::Hz, 5000.0f);
        param::addChoice (l, "fbSlope", "Damp Slope", { "1", "2", "3", "4" }, 0);
        param::addFloat (l, "feedback", "Feedback", 0.0f, 110.0f, 35.0f, U::Percent);
        param::addFloat (l, "width", "Width", 0.0f, 100.0f, 100.0f, U::Percent);
        param::addBool (l, "clean", "Clean", false);
        param::addBool (l, "dist", "Dist", false);
        addFxCommon (l);
        return l;
    }

    Info getInfo() const override
    {
        Info i; LTP_BASE_INFO (i)
        i.dynamics = false;
        i.editorWidth = 860;
        return i;
    }

    std::vector<Section> getSections() const override
    {
        return {
            delaytiming::section (2),
            { "BUCKET BRIGADE", 0, { knob ("inHp", "INPUT HP"),
                                     knob ("inLp", "INPUT LP"),
                                     knob ("lowDamp", "LOW DAMP"),
                                     knob ("highDamp", "HIGH DAMP"),
                                     segment ("fbSlope", "SLOPE"),
                                     knob ("feedback", "FEEDBACK", "Above 100% the repeats build up (held back by the BBD's saturation)"),
                                     knob ("width", "WIDTH"),
                                     toggle ("clean", "CLEAN", "No noise or grit - still band-limited like a real BBD"),
                                     toggle ("dist", "DIST", "Non-linear feedback path") } },
            outputSection()
        };
    }

    double tailSeconds() const override { return 12.0; }

protected:
    void prepareFx (double sampleRate, int) override
    {
        const int maxS = (int) (sampleRate * (delaytiming::maxMs / 1000.0 + 0.1));
        for (auto& d : line) d.prepare (maxS);
        for (int c = 0; c < 2; ++c) { dS[c].prepare (sampleRate, 120.0f); wander[c].setRate (sampleRate, 0.31f + 0.07f * (float) c); }
        const auto t = delaytiming::times (apvts, hostBpm.load());
        dS[0].reset (t.first * 0.001f * (float) sampleRate);
        dS[1].reset (t.second * 0.001f * (float) sampleRate);
        noise.seed (77);
        envCoef = msCoef (30.0f, sampleRate);
        lastBw[0] = lastBw[1] = -1.0f;
        lastHp = lastLp = lastLd = lastHd = -1.0f;
    }

    void resetFx() override
    {
        for (auto& d : line) d.reset();
        hp.reset(); lp.reset();
        for (int c = 0; c < 2; ++c) { bw[c].reset(); lowD[c].reset(); for (auto& f : highD[c]) f.reset(); env[c] = 0.0f; }
    }

    void beginBlock() override
    {
        const auto t = delaytiming::times (apvts, hostBpm.load());
        dS[0].setTarget (t.first * 0.001f * (float) sr);
        dS[1].setTarget (t.second * 0.001f * (float) sr);
        for (int c = 0; c < 2; ++c)
        {
            // BBD bandwidth: ~8192-stage equivalent, Nyquist = N / (4 * delay)
            const float sec = (c == 0 ? t.first : t.second) * 0.001f;
            const float f = clampf (8192.0f / (4.0f * std::max (sec, 0.01f)), 1500.0f, 15000.0f);
            if (std::abs (f - lastBw[c]) > 1.0f) { lastBw[c] = f; bw[c].set (Biquad::LowPass, sr, f); }
        }
        auto changed = [] (float a, float& last) { if (std::abs (a - last) > 0.01f) { last = a; return true; } return false; };
        const float h = pv ("inHp"), lo = pv ("inLp"), ld = pv ("lowDamp"), hd = pv ("highDamp");
        if (changed (h, lastHp)) hp.set (Biquad::HighPass, sr, h, 0.707f);
        if (changed (lo, lastLp)) lp.set (Biquad::LowPass, sr, lo, 0.707f);
        if (changed (ld, lastLd)) for (auto& f : lowD) f.set (sr, ld);
        if (changed (hd, lastHd)) for (auto& c : highD) for (auto& f : c) f.set (sr, hd);
        hpOn = h > 21.0f; lpOn = lo < 19900.0f;
        slope = pi ("fbSlope") + 1;
        fb = pv ("feedback") * 0.01f;
        width = pv ("width") * 0.01f;
        clean = pb ("clean");
        dist = pb ("dist");
    }

    void processFrame (float inL, float inR, float& wetL, float& wetR) override
    {
        float m = 0.5f * (inL + inR);
        if (hpOn) m = hp.process (m); else hp.process (m);
        if (lpOn) m = lp.process (m); else lp.process (m);

        float out[2];
        for (int c = 0; c < 2; ++c)
        {
            wander[c].advance();
            const float d = dS[c].next() * (1.0f + (clean ? 0.0f : 0.0015f * wander[c].value (Lfo::Sine)));
            float y = bw[c].process (line[c].read (std::max (1.0f, d) - 1.0f));   // reconstruction filter

            // feedback loop: damping, optional non-linearity
            float f = lowD[c].hp (y);
            for (int k = 0; k < slope; ++k) f = highD[c][k].lp (f);
            f *= fb;
            f = dist ? fastTanh (f * 2.2f) * 0.6f : softSat (f, 0.15f);

            float w = m + f;
            if (! clean)
            {
                const float a = std::abs (w);
                env[c] = a + envCoef * (env[c] - a);
                w = softSat (w, 0.25f);
                w += noise.next() * 0.00012f * (0.25f + 4.0f * env[c]);   // compander breathing
            }
            line[c].push (w);
            out[c] = y;
        }
        const float mid = 0.5f * (out[0] + out[1]), side = 0.5f * (out[0] - out[1]) * width;
        wetL = mid + side;
        wetR = mid - side;
    }

private:
    DelayLine line[2];
    Smoother dS[2];
    Filter24 bw[2];
    Biquad hp, lp;
    OnePole lowD[2], highD[2][4];
    Lfo wander[2];
    Noise noise;
    float env[2] {}, envCoef = 0, lastBw[2] { -1, -1 }, lastHp = -1, lastLp = -1, lastLd = -1, lastHd = -1;
    float fb = 0.35f, width = 1;
    int slope = 1;
    bool hpOn = false, lpOn = false, clean = false, dist = false;
};

LTP_REGISTER (BbdDelay)
