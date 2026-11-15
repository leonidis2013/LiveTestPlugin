// SPDX-License-Identifier: AGPL-3.0-only
// Copyright (C) 2026 Jerod Ulicki

// Distort Comp (SQ: CompStortion) - modeled on the modern multi-mode "distortion" compressor.
//  - Fixed threshold: INPUT drives into it, OUTPUT trims. ATTACK/RELEASE 0-10.
//  - Ratio modes each change the knee and the ballistics (1:1 still colours the sound),
//    plus SMASH (hard-knee brick-wall limiter) and BRIT (the 76's "all buttons in" feel).
//  - Distortion: A (2nd harmonic), B (A, more aggressive at low reduction),
//    C (3rd harmonic), D (C, more aggressive at low reduction).
//  - Detector: HP lets lows pass without triggering, BAND PASS makes it more sensitive
//    around 6-7 kHz; they can be combined.

#include "Comp.h"

using namespace ltp;

class DistortComp : public CompBase
{
public:
    DistortComp() : CompBase (createLayout()) {}

    static Layout createLayout()
    {
        using U = param::Unit;
        Layout l;
        addCommon (l);
        param::addFloat (l, "input", "Input", 0.0f, 30.0f, 0.0f, U::Db, -1.0f, 0.1f);
        param::addFloat (l, "output", "Output", -24.0f, 12.0f, 0.0f, U::Db, -1.0f, 0.1f);
        param::addFloat (l, "attack", "Attack", 0.0f, 10.0f, 5.0f, U::Plain, -1.0f, 0.01f, 1);
        param::addFloat (l, "release", "Release", 0.0f, 10.0f, 5.0f, U::Plain, -1.0f, 0.01f, 1);
        param::addChoice (l, "ratio", "Ratio", { "1:1", "2:1", "3:1", "4:1", "6:1", "10:1", "20:1", "Smash", "Brit" }, 3);
        param::addChoice (l, "dist", "Distortion", { "Off", "A", "B", "C", "D" }, 0);
        param::addBool (l, "detHp", "Detector HP", false);
        param::addBool (l, "detBp", "Detector Band Pass", false);
        addCompCommon (l);
        return l;
    }

    Info getInfo() const override
    {
        Info i; LTP_BASE_INFO (i)
        i.graph = Graph::Compressor;
        i.editorWidth = 900;
        i.maxGrDb = 30.0f;
        return i;
    }

    std::vector<Section> getSections() const override
    {
        return {
            { "COMPRESSOR", 0, { knob ("input", "INPUT", "Drives into the fixed threshold"),
                                 knob ("attack", "ATTACK", "0 = fastest, 10 = slowest"),
                                 knob ("release", "RELEASE", "0 = fastest, 10 = slowest"),
                                 knob ("output", "OUTPUT"),
                                 choice ("ratio", "RATIO MODE", "Each ratio mode has its own knee and ballistics. Smash = brick-wall limiter, Brit = 'all buttons in' style") } },
            { "DISTORTION & DETECTOR", 2, { segment ("dist", "DISTORTION", "A/B: 2nd harmonic. C/D: 3rd harmonic. B and D are more aggressive at lower gain reduction"),
                                            toggle ("detHp", "HP", "Low frequencies pass without triggering the compressor"),
                                            toggle ("detBp", "BAND PASS", "Detector more sensitive around 6-7 kHz") } },
            sidechainSection()
        };
    }

    struct Mode { float ratio, knee, attMul, relMul, overshoot; };
    static Mode modeFor (int idx)
    {
        static const Mode modes[] = {
            { 1.0f, 0.0f, 1.0f, 1.0f, 0.0f },     // 1:1 - colour only
            { 2.0f, 18.0f, 1.3f, 1.4f, 0.0f },    // gentle, opto-like knee
            { 3.0f, 14.0f, 1.1f, 1.2f, 0.0f },
            { 4.0f, 10.0f, 1.0f, 1.0f, 0.0f },
            { 6.0f, 8.0f, 1.0f, 1.0f, 0.0f },
            { 10.0f, 6.0f, 1.3f, 1.8f, 0.0f },    // smooth, longer release
            { 20.0f, 3.0f, 0.9f, 1.0f, 0.0f },
            { 100.0f, 0.0f, 0.3f, 0.8f, 0.0f },   // Smash
            { 100.0f, 2.0f, 0.5f, 0.5f, 0.15f }   // Brit
        };
        return modes[juce::jlimit (0, 8, idx)];
    }

    static constexpr float threshold = -20.0f;   // dBFS, fixed

    static float curveGr (float drivenDb, int idx)
    {
        const auto m = modeFor (idx);
        if (m.ratio <= 1.0f) return 0.0f;
        return -compressorGainDb (drivenDb, threshold, m.ratio, m.knee) + m.overshoot * std::max (0.0f, drivenDb - threshold);
    }

protected:
    void prepareModel (double sampleRate) override
    {
        hp.set (sampleRate, 100.0f);
        bp.set (Biquad::Peak, sampleRate, 6500.0f, 1.2f, 9.0f);
        for (auto& d : dc) d.prepare (sampleRate);
    }

    void resetModel() override { gr = 0.0f; hp.reset(); bp.reset(); for (auto& d : dc) d.reset(); }

    void beginBlock() override
    {
        inGain = dbToGain (pv ("input"));
        out = dbToGain (pv ("output"));
        mode = pi ("ratio");
        dist = pi ("dist");
        useHp = pb ("detHp");
        useBp = pb ("detBp");
        const auto m = modeFor (mode);
        const float attMs = 0.05f * std::pow (30.0f / 0.05f, pv ("attack") / 10.0f) * m.attMul;
        const float relMs = 50.0f * std::pow (3000.0f / 50.0f, pv ("release") / 10.0f) * m.relMul;
        attCoef = msCoef (attMs, sr);
        relCoef = msCoef (relMs, sr);
    }

    float processFrame (float& l, float& r, float scL, float scR) override
    {
        l *= inGain; r *= inGain;
        float det = 0.5f * (scL + scR) * inGain;
        const float detPeak = linkPeak (scL, scR) * inGain;
        if (useHp) det = hp.hp (det); else hp.lp (det);
        if (useBp) det = bp.process (det); else bp.process (det);
        const float level = (useHp || useBp) ? std::abs (det) * 1.2f : detPeak;
        const float target = curveGr (gainToDb (level), mode);
        gr = target + (target > gr ? attCoef : relCoef) * (gr - target);

        const float g = dbToGain (-gr);
        l *= g; r *= g;

        if (dist > 0)
        {
            const bool aggressive = dist == 2 || dist == 4;
            const float amt = aggressive ? 0.35f + gr * 0.02f : 0.04f + gr * 0.03f;
            const float d = std::min (amt, 1.2f);
            if (dist <= 2) { l = dc[0].process (tubeStage (l, d)); r = dc[1].process (tubeStage (r, d)); }
            else           { l = softSat (l, d); r = softSat (r, d); }
        }
        if (mode == 8) { l = softSat (l, 0.15f + gr * 0.01f); r = softSat (r, 0.15f + gr * 0.01f); }
        l *= out; r *= out;
        return gr;
    }

    float staticGainDb (float in) const override
    {
        const float inDb = pv ("input");
        return inDb - curveGr (in + inDb, pi ("ratio")) + pv ("output");
    }

private:
    OnePole hp;
    Biquad bp;
    DcBlocker dc[2];
    float inGain = 1, out = 1, attCoef = 0, relCoef = 0, gr = 0;
    int mode = 3, dist = 0;
    bool useHp = false, useBp = false;
};

LTP_REGISTER (DistortComp)
