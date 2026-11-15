// SPDX-License-Identifier: AGPL-3.0-only
// Copyright (C) 2026 Jerod Ulicki

// Punch Comp (SQ: Mighty) - modeled on the transistor-array VCA compressor.
// Aggressive, very fast attack with a high fixed ratio; three controls (THRESHOLD, RELEASE,
// OUTPUT) and a DETECTOR switch with two voices:
//   1 - peak detector: instant grab, bright and snappy (kick, snare)
//   2 - averaging detector: a touch slower, rounder and thicker (bass, parallel smash)
// The transistor array adds odd-harmonic edge as it works harder.

#include "Comp.h"

using namespace ltp;

class PunchComp : public CompBase
{
public:
    PunchComp() : CompBase (createLayout()) {}

    static Layout createLayout()
    {
        using U = param::Unit;
        Layout l;
        addCommon (l);
        param::addFloat (l, "thresh", "Threshold", -46.0f, 18.0f, 0.0f, U::Dbu, -1.0f, 0.1f);
        param::addFloat (l, "release", "Release", 20.0f, 2000.0f, 120.0f, U::Ms, 250.0f);
        param::addFloat (l, "output", "Output", 0.0f, 24.0f, 0.0f, U::Db, -1.0f, 0.1f);
        param::addChoice (l, "detector", "Detector", { "1", "2" }, 0);
        addCompCommon (l);
        return l;
    }

    Info getInfo() const override
    {
        Info i; LTP_BASE_INFO (i)
        i.graph = Graph::Compressor;
        i.editorWidth = 700;
        i.maxGrDb = 30.0f;
        return i;
    }

    std::vector<Section> getSections() const override
    {
        return {
            { "MIGHTY-STYLE COMPRESSOR", 0, { knob ("thresh", "THRESHOLD", "Threshold in dBu, as on the SQ"),
                                              knob ("release", "RELEASE"),
                                              knob ("output", "OUTPUT", "Make-up gain"),
                                              segment ("detector", "DETECTOR", "1: peak voice (snappy). 2: averaging voice (rounder, thicker)") } },
            sidechainSection()
        };
    }

    static constexpr float ratio = 10.0f, knee = 3.0f;

protected:
    void prepareModel (double sampleRate) override
    {
        rms.set (sampleRate, 3.0f);
        for (auto& d : dc) d.prepare (sampleRate);
    }

    void resetModel() override { rms.reset(); gr = 0.0f; for (auto& d : dc) d.reset(); }

    void beginBlock() override
    {
        thr = dbuToDbfs (pv ("thresh"));
        voice2 = pi ("detector") == 1;
        attCoef = msCoef (voice2 ? 1.2f : 0.05f, sr);
        relMs = pv ("release");
        out = dbToGain (pv ("output"));
    }

    float processFrame (float& l, float& r, float scL, float scR) override
    {
        const float level = voice2 ? rms.process (linkPeak (scL, scR)) * 1.41421356f : linkPeak (scL, scR);
        const float target = -compressorGainDb (gainToDb (level), thr, ratio, knee);
        if (target > gr) gr = target + attCoef * (gr - target);
        else
        {
            // quicker recovery from short peaks, the set release once it is working hard
            const float ms = relMs * (0.5f + 0.5f * clampf (gr / 10.0f, 0.0f, 1.0f));
            gr = target + std::exp (-1.0f / (ms * 0.001f * (float) sr)) * (gr - target);
        }
        const float g = dbToGain (-gr);
        const float drive = 0.05f + gr * (voice2 ? 0.010f : 0.014f);
        l = dc[0].process (softSat (l * g, drive) + 0.02f * tubeStage (l * g, drive)) * out;
        r = dc[1].process (softSat (r * g, drive) + 0.02f * tubeStage (r * g, drive)) * out;
        return gr;
    }

    float staticGainDb (float in) const override
    {
        return compressorGainDb (in, dbuToDbfs (pv ("thresh")), ratio, knee) + pv ("output") + 20.0f * std::log10 (1.02f);
    }

private:
    RmsDetector rms;
    DcBlocker dc[2];
    float thr = 0, attCoef = 0, relMs = 120, out = 1, gr = 0;
    bool voice2 = false;
};

LTP_REGISTER (PunchComp)
