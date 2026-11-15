// SPDX-License-Identifier: AGPL-3.0-only
// Copyright (C) 2026 Jerod Ulicki

// Bus Comp (SQ: Bus) - modeled on the British console VCA bus compressor.
//  - Stepped controls like the hardware: ratio 2/4/10, attack 0.1-30 ms,
//    release 0.1/0.3/0.6/1.2 s or AUTO.
//  - Feed-forward VCA, stereo linked, ratio-dependent knee (softer at 2:1).
//  - AUTO release is two-stage: a fast recovery for transients riding on a slow one for
//    sustained material - the "glue" with minimal pumping.

#include "Comp.h"

using namespace ltp;

class BusComp : public CompBase
{
public:
    BusComp() : CompBase (createLayout()) {}

    static Layout createLayout()
    {
        using U = param::Unit;
        Layout l;
        addCommon (l);
        param::addFloat (l, "thresh", "Threshold", -30.0f, 18.0f, 0.0f, U::Dbu, -1.0f, 0.1f);
        param::addChoice (l, "ratio", "Ratio", { "2", "4", "10" }, 1);
        param::addChoice (l, "attack", "Attack", { "0.1", "0.3", "1", "3", "10", "30" }, 4);
        param::addChoice (l, "release", "Release", { "0.1", "0.3", "0.6", "1.2", "Auto" }, 4);
        param::addFloat (l, "makeup", "Make-up", 0.0f, 20.0f, 0.0f, U::Db, -1.0f, 0.1f);
        addCompCommon (l);
        return l;
    }

    Info getInfo() const override
    {
        Info i; LTP_BASE_INFO (i)
        i.graph = Graph::Compressor;
        i.editorWidth = 820;
        i.maxGrDb = 20.0f;
        return i;
    }

    std::vector<Section> getSections() const override
    {
        return {
            { "BUS COMPRESSOR", 0, { knob ("thresh", "THRESHOLD", "Threshold in dBu, as on the SQ"),
                                     segment ("ratio", "RATIO"),
                                     segment ("attack", "ATTACK (ms)"),
                                     segment ("release", "RELEASE (s)"),
                                     knob ("makeup", "MAKE-UP") } },
            sidechainSection()
        };
    }

    static float kneeFor (int ratioIdx) { return ratioIdx == 0 ? 10.0f : ratioIdx == 1 ? 6.0f : 3.0f; }
    static float ratioFor (int ratioIdx) { return ratioIdx == 0 ? 2.0f : ratioIdx == 1 ? 4.0f : 10.0f; }

protected:
    void prepareModel (double sampleRate) override
    {
        det.set (sampleRate, 1.0f);
        for (auto& d : dc) d.prepare (sampleRate);
    }

    void resetModel() override { det.reset(); fast = slow = 0.0f; for (auto& d : dc) d.reset(); }

    void beginBlock() override
    {
        static constexpr float attackMs[] = { 0.1f, 0.3f, 1.0f, 3.0f, 10.0f, 30.0f };
        static constexpr float releaseMs[] = { 100.0f, 300.0f, 600.0f, 1200.0f };
        thr = dbuToDbfs (pv ("thresh"));
        ratioIdx = pi ("ratio");
        makeup = dbToGain (pv ("makeup"));
        const float att = attackMs[juce::jlimit (0, 5, pi ("attack"))];
        const int rel = pi ("release");
        autoRelease = rel == 4;
        attCoef = msCoef (att, sr);
        relCoef = msCoef (autoRelease ? 120.0f : releaseMs[juce::jlimit (0, 3, rel)], sr);
        slowAttCoef = msCoef (std::max (att * 4.0f, 60.0f), sr);
        slowRelCoef = msCoef (1500.0f, sr);
    }

    float processFrame (float& l, float& r, float scL, float scR) override
    {
        const float level = det.process (linkPeak (scL, scR)) * 1.41421356f;
        const float target = -compressorGainDb (gainToDb (level), thr, ratioFor (ratioIdx), kneeFor (ratioIdx));

        fast = target + (target > fast ? attCoef : relCoef) * (fast - target);
        float gr = fast;
        if (autoRelease)
        {
            const float st = target * 0.6f;
            slow = st + (st > slow ? slowAttCoef : slowRelCoef) * (slow - st);
            gr = std::max (fast, slow);
        }

        const float g = dbToGain (-gr) * makeup;
        l = dc[0].process (tubeStage (l * g, 0.02f + gr * 0.002f));
        r = dc[1].process (tubeStage (r * g, 0.02f + gr * 0.002f));
        return gr;
    }

    float staticGainDb (float in) const override
    {
        const int idx = pi ("ratio");
        return compressorGainDb (in, dbuToDbfs (pv ("thresh")), ratioFor (idx), kneeFor (idx)) + pv ("makeup");
    }

private:
    RmsDetector det;
    DcBlocker dc[2];
    float thr = 0, makeup = 1, attCoef = 0, relCoef = 0, slowAttCoef = 0, slowRelCoef = 0, fast = 0, slow = 0;
    int ratioIdx = 1;
    bool autoRelease = true;
};

LTP_REGISTER (BusComp)
