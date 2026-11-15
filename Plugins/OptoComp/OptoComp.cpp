// SPDX-License-Identifier: AGPL-3.0-only
// Copyright (C) 2026 Jerod Ulicki

// Opto (SQ: Opto) - a composite optical compressor.
// Optical cells react to the light source with a smooth, programme-dependent recovery:
//  - TRANSIENT: electro-luminescent recovery - fast for the first part of the release,
//    slowing as the gain reduction drops (the light panel's non-linear decay).
//  - BURN: filament heat memory - sustained compression heats the lamp, so recovery gets
//    much slower after long, heavy passages.
// Wide soft knee, RMS-like detection, Fast/Med/Slow attack and recovery like the SQ model.

#include "Comp.h"

using namespace ltp;

class OptoComp : public CompBase
{
public:
    OptoComp() : CompBase (createLayout()) {}

    static Layout createLayout()
    {
        using U = param::Unit;
        Layout l;
        addCommon (l);
        param::addFloat (l, "thresh", "Threshold", -46.0f, 18.0f, 0.0f, U::Dbu, -1.0f, 0.1f);
        param::addFloat (l, "ratio", "Ratio", 1.0f, 20.0f, 3.0f, U::Ratio, 4.0f, 0.01f);
        param::addFloat (l, "gain", "Gain", 0.0f, 24.0f, 0.0f, U::Db, -1.0f, 0.1f);
        param::addChoice (l, "attack", "Attack", { "Fast", "Med", "Slow" }, 0);
        param::addChoice (l, "recovery", "Recovery", { "Fast", "Med", "Slow" }, 1);
        param::addBool (l, "transient", "Transient", true);
        param::addBool (l, "burn", "Burn", false);
        addCompCommon (l);
        return l;
    }

    Info getInfo() const override
    {
        Info i; LTP_BASE_INFO (i)
        i.graph = Graph::Compressor;
        i.editorWidth = 760;
        return i;
    }

    std::vector<Section> getSections() const override
    {
        return {
            { "OPTO", 0, { knob ("thresh", "THRESHOLD", "Threshold in dBu, as on the SQ"),
                           knob ("ratio", "RATIO"),
                           knob ("gain", "GAIN", "Make-up gain"),
                           segment ("attack", "ATTACK"),
                           segment ("recovery", "RECOVERY") } },
            { "CHARACTER", 3, { toggle ("transient", "TRANSIENT", "Electro-luminescent recovery: fast at first, slowing as gain reduction drops - punchy without distortion"),
                                toggle ("burn", "BURN", "Filament heat memory: after heavy, sustained compression the recovery becomes very slow") } },
            sidechainSection()
        };
    }

protected:
    static constexpr float kneeDb = 12.0f;

    void prepareModel (double sampleRate) override
    {
        rms.set (sampleRate, 3.0f);
        heatCoef = msCoef (2500.0f, sampleRate);
        heatCoolCoef = msCoef (6000.0f, sampleRate);
    }

    void resetModel() override { rms.reset(); gr = 0.0f; heat = 0.0f; }

    void beginBlock() override
    {
        static constexpr float attackMs[] = { 3.0f, 10.0f, 30.0f };
        static constexpr float recoveryMs[] = { 90.0f, 350.0f, 1200.0f };
        thr = dbuToDbfs (pv ("thresh"));
        ratio = pv ("ratio");
        makeup = dbToGain (pv ("gain"));
        attCoef = msCoef (attackMs[juce::jlimit (0, 2, pi ("attack"))], sr);
        recMs = recoveryMs[juce::jlimit (0, 2, pi ("recovery"))];
        transient = pb ("transient");
        burn = pb ("burn");
    }

    float processFrame (float& l, float& r, float scL, float scR) override
    {
        const float level = rms.process (linkPeak (scL, scR)) * 1.41421356f;   // RMS -> peak-equivalent for a sine
        const float target = -compressorGainDb (gainToDb (level), thr, ratio, kneeDb);

        if (target > gr)
            gr = target + attCoef * (gr - target);
        else
        {
            float ms = recMs;
            if (transient)   // fast while reduction is large, slower as it fades
                ms *= 0.3f + 1.7f * (1.0f - clampf (gr / 8.0f, 0.0f, 1.0f));
            if (burn)
                ms *= 1.0f + 6.0f * heat;
            const float c = std::exp (-1.0f / (ms * 0.001f * (float) sr));
            gr = target + c * (gr - target);
        }

        // lamp heat follows sustained gain reduction (heats faster than it cools)
        const float heatTarget = clampf (gr / 10.0f, 0.0f, 1.0f);
        heat = heatTarget + (heatTarget > heat ? heatCoef : heatCoolCoef) * (heat - heatTarget);

        const float g = dbToGain (-gr) * makeup;
        l *= g; r *= g;
        return gr;
    }

    float staticGainDb (float in) const override
    {
        return compressorGainDb (in, dbuToDbfs (pv ("thresh")), pv ("ratio"), kneeDb) + pv ("gain");
    }

private:
    RmsDetector rms;
    float thr = 0, ratio = 3, makeup = 1, attCoef = 0, recMs = 350, gr = 0, heat = 0, heatCoef = 0, heatCoolCoef = 0;
    bool transient = true, burn = false;
};

LTP_REGISTER (OptoComp)
