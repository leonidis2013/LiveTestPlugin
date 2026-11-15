// SPDX-License-Identifier: AGPL-3.0-only
// Copyright (C) 2026 Jerod Ulicki

// Tap Delay (SQ: Stereo Tap Delay) - clean stereo digital delay, up to 2.7 s.
//  - Input HP/LP filters, feedback filter with selectable slope (higher = darker repeats).
//  - Classic ping-pong, or SCATTER: a single echo on one side and a regenerating pattern on
//    the other. WIDTH runs from mono to wide stereo.

#include "Common/DelayTiming.h"

using namespace ltp;

class TapDelay : public FxBase
{
public:
    TapDelay() : FxBase (createLayout()) {}

    static Layout createLayout()
    {
        using U = param::Unit;
        Layout l;
        addCommon (l);
        delaytiming::addParams (l);
        delaytiming::addInputFilters (l);
        param::addFloat (l, "fbFreq", "Feedback Filter", 500.0f, 20000.0f, 8000.0f, U::Hz, 4000.0f);
        param::addChoice (l, "fbSlope", "Feedback Slope", { "1", "2", "3", "4" }, 0);
        param::addFloat (l, "feedback", "Feedback", 0.0f, 100.0f, 30.0f, U::Percent);
        param::addFloat (l, "width", "Width", 0.0f, 100.0f, 100.0f, U::Percent);
        param::addBool (l, "scatter", "Scatter", false);
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
            delaytiming::section (5),
            { "FILTERS & FEEDBACK", 1, { knob ("inHp", "INPUT HP"),
                                         knob ("inLp", "INPUT LP"),
                                         knob ("fbFreq", "FB FILTER", "Low-pass in the feedback loop: each repeat gets darker"),
                                         segment ("fbSlope", "FB SLOPE", "Higher = more high-frequency loss per repeat"),
                                         knob ("feedback", "FEEDBACK"),
                                         knob ("width", "WIDTH", "Mono to wide stereo"),
                                         toggle ("scatter", "SCATTER", "Off: classic ping-pong. On: single echo on one side, regenerating repeats on the other") } },
            outputSection()
        };
    }

    Faceplate getFaceplate() const override
    {
        Faceplate f;
        f.title = "STEREO TAP DELAY"; f.subtitle = "Digital delay  -  SQ model: Stereo Tap Delay";
        f.panel = 0xff2c3036; f.ink = 0xffdfe4ea; f.finish = Faceplate::Brushed; f.knobs = Faceplate::Silver; f.led = 0xff47ff6e;
        f.rows = { { fpDisplay ("bpm", "TEMPO"), fpTap(), fpButtons ("timeMode", "MODE"), fpButtons ("tempoSrc", "TEMPO"),
                     fpStepped ("noteL", "NOTE L", 0), fpStepped ("noteR", "NOTE R", 0), fpPush ("link", "LINK") },
                   { fpDisplay ("timeL", "TIME L"), fpKnob ("timeL", "TIME L", 0), fpKnob ("timeR", "TIME R", 0),
                     fpKnob ("feedback", "FEEDBACK", 1), fpKnob ("width", "WIDTH", 0), fpPush ("scatter", "SCATTER") },
                   { fpKnob ("inHp", "INPUT HP", 0), fpKnob ("inLp", "INPUT LP", 0), fpKnob ("fbFreq", "FB FILTER", 0), fpButtons ("fbSlope", "FB SLOPE") } };
        return f;
    }

    double tailSeconds() const override { return 10.0; }

protected:
    void prepareFx (double sampleRate, int) override
    {
        const int maxS = (int) (sampleRate * (delaytiming::maxMs / 1000.0 + 0.1));
        for (auto& d : line) d.prepare (maxS);
        lastHp = lastLp = lastFb = -1.0f;
        dS[0].prepare (sampleRate, 80.0f); dS[1].prepare (sampleRate, 80.0f);
        const auto t = delaytiming::times (apvts, hostBpm.load());
        dS[0].reset (t.first * 0.001f * (float) sampleRate);
        dS[1].reset (t.second * 0.001f * (float) sampleRate);
    }

    void resetFx() override
    {
        for (auto& d : line) d.reset();
        hp.reset(); lp.reset();
        for (auto& c : fbLp) for (auto& f : c) f.reset();
    }

    void beginBlock() override
    {
        const auto t = delaytiming::times (apvts, hostBpm.load());
        dS[0].setTarget (t.first * 0.001f * (float) sr);
        dS[1].setTarget (t.second * 0.001f * (float) sr);
        const float h = pv ("inHp"), lo = pv ("inLp"), f = pv ("fbFreq");
        if (std::abs (h - lastHp) > 0.01f) { lastHp = h; hp.set (Biquad::HighPass, sr, h, 0.707f); }
        if (std::abs (lo - lastLp) > 0.01f) { lastLp = lo; lp.set (Biquad::LowPass, sr, lo, 0.707f); }
        if (std::abs (f - lastFb) > 0.01f) { lastFb = f; for (auto& c : fbLp) for (auto& p : c) p.set (sr, f); }
        hpOn = h > 21.0f; lpOn = lo < 19900.0f;
        slope = pi ("fbSlope") + 1;
        fb = pv ("feedback") * 0.01f;
        width = pv ("width") * 0.01f;
        scatter = pb ("scatter");
    }

    void processFrame (float inL, float inR, float& wetL, float& wetR) override
    {
        float m = 0.5f * (inL + inR);
        if (hpOn) m = hp.process (m); else hp.process (m);
        if (lpOn) m = lp.process (m); else lp.process (m);

        const float outL = line[0].read (std::max (1.0f, dS[0].next()) - 1.0f);
        const float outR = line[1].read (std::max (1.0f, dS[1].next()) - 1.0f);
        auto filt = [this] (int ch, float x) { for (int k = 0; k < slope; ++k) x = fbLp[ch][k].lp (x); return x; };

        if (scatter)
        {
            line[0].push (m);                                    // single echo
            line[1].push (m + fb * filt (1, outR));              // regenerating pattern
        }
        else
        {
            line[0].push (m + fb * filt (1, outR));              // ping-pong: L -> R -> L ...
            line[1].push (fb * filt (0, outL));
        }

        const float mid = 0.5f * (outL + outR), side = 0.5f * (outL - outR) * width;
        wetL = mid + side;
        wetR = mid - side;
    }

private:
    DelayLine line[2];
    Smoother dS[2];
    Biquad hp, lp;
    OnePole fbLp[2][4];
    float lastHp = -1, lastLp = -1, lastFb = -1, fb = 0.3f, width = 1;
    int slope = 1;
    bool hpOn = false, lpOn = false, scatter = false;
};

LTP_REGISTER (TapDelay)
