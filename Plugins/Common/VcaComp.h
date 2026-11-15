// SPDX-License-Identifier: AGPL-3.0-only
// Copyright (C) 2026 Jerod Ulicki

#pragma once

// VCA true-RMS compressors (SQ: 16T and 16VU), modeled on the classic live-sound VCA units.
//  - Three controls, like the hardware: THRESHOLD, COMPRESSION (ratio) and OUTPUT.
//  - True-RMS detection; attack is programme dependent (the bigger the overshoot, the faster
//    it clamps - about 15 ms for 10 dB, 5 ms for 20 dB) and release is a constant rate
//    in dB per second (~120 dB/s).
//  - 16VU: the older unit - slower detector, more VCA non-linearity (2nd harmonic) that
//    thickens the low mids when driven, and a needle-style GR meter.

#include "Comp.h"

namespace ltp
{
    template <bool Vu>
    class VcaComp : public CompBase
    {
    public:
        VcaComp() : CompBase (createLayout()) {}

        static Layout createLayout()
        {
            using U = param::Unit;
            Layout l;
            addCommon (l);
            param::addFloat (l, "thresh", "Threshold", -40.0f, 20.0f, 0.0f, U::Dbu, -1.0f, 0.1f);
            param::addFloat (l, "ratio", "Compression", 1.0f, 100.0f, 4.0f, U::Ratio, 6.0f, 0.01f);
            param::addFloat (l, "output", "Output", -20.0f, 20.0f, 0.0f, U::Db, -1.0f, 0.1f);
            addCompCommon (l);
            return l;
        }

        Info getInfo() const override
        {
            Info i;
            i.graph = Graph::Compressor;
            i.editorWidth = 700;
            i.maxGrDb = Vu ? 20.0f : 24.0f;
            return i;
        }

        std::vector<Section> getSections() const override
        {
            return {
                { Vu ? "16VU" : "16T", 0, { knob ("thresh", "THRESHOLD", "Threshold in dBu, as on the SQ"),
                                            knob ("ratio", "COMPRESSION", "Ratio, 1:1 to infinity:1"),
                                            knob ("output", "OUTPUT") } },
                sidechainSection()
            };
        }

    protected:
        void prepareModel (double sampleRate) override
        {
            rms.set (sampleRate, Vu ? 12.0f : 6.0f);
            relPerSample = (Vu ? 125.0f : 120.0f) / (float) sampleRate;
            for (auto& d : dc) d.prepare (sampleRate);
            for (auto& b : body) b.set (Biquad::BandPass, sampleRate, 220.0f, 0.8f);
        }

        void resetModel() override { rms.reset(); gr = 0.0f; for (auto& d : dc) d.reset(); for (auto& b : body) b.reset(); }

        void beginBlock() override
        {
            thr = dbuToDbfs (pv ("thresh"));
            ratio = pv ("ratio");
            out = dbToGain (pv ("output"));
        }

        float processFrame (float& l, float& r, float scL, float scR) override
        {
            const float level = rms.process (linkPeak (scL, scR)) * 1.41421356f;
            const float target = -compressorGainDb (gainToDb (level), thr, ratio, 0.0f);

            if (target > gr)
            {
                const float over = target - gr;
                const float ms = clampf ((Vu ? 22.0f : 15.0f) * 10.0f / std::max (over, 1.0f), Vu ? 4.0f : 2.5f, Vu ? 30.0f : 18.0f);
                gr = target + std::exp (-1.0f / (ms * 0.001f * (float) sr)) * (gr - target);
            }
            else
                gr = std::max (target, gr - relPerSample);

            const float g = dbToGain (-gr);
            l *= g; r *= g;

            // VCA non-linearity grows with gain reduction
            const float drive = (Vu ? 0.05f : 0.015f) + gr * (Vu ? 0.012f : 0.003f);
            l = dc[0].process (tubeStage (l, drive));
            r = dc[1].process (tubeStage (r, drive));
            if constexpr (Vu)
            {
                // driven 16VU: saturated low-mid "body" is added back in
                const float amt = clampf (gr / 15.0f, 0.0f, 1.0f) * 0.35f;
                if (amt > 0.001f)
                {
                    l += amt * softSat (body[0].process (l), 1.0f);
                    r += amt * softSat (body[1].process (r), 1.0f);
                }
                else { body[0].process (l); body[1].process (r); }
            }
            l *= out; r *= out;
            return gr;
        }

        float staticGainDb (float in) const override
        {
            return compressorGainDb (in, dbuToDbfs (pv ("thresh")), pv ("ratio"), 0.0f) + pv ("output");
        }

    private:
        RmsDetector rms;
        DcBlocker dc[2];
        Biquad body[2];
        float thr = 0, ratio = 4, out = 1, gr = 0, relPerSample = 0.0025f;
    };
}
