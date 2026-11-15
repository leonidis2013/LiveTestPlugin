// SPDX-License-Identifier: AGPL-3.0-only
// Copyright (C) 2026 Jerod Ulicki

#pragma once

// Base for the FX plugins. On the SQ an FX unit sits on an FX send and returns only the
// effect, so MIX defaults to 100% (effect only). Lower it to use a plugin as an insert.

#include "Base.h"

namespace ltp
{
    class FxBase : public Processor
    {
    public:
        using Processor::Processor;

        static void addFxCommon (Layout& l, float defaultMix = 100.0f)
        {
            param::addFloat (l, "mix", "Mix", 0.0f, 100.0f, defaultMix, param::Unit::Percent);
            param::addFloat (l, "level", "Return Level", -24.0f, 12.0f, 0.0f, param::Unit::Db, -1.0f, 0.1f);
        }

        static Section outputSection (int colour = 6)
        {
            return { "OUTPUT", colour,
                     { knob ("mix", "MIX", "100% = effect only, like an SQ FX return. Lower it to blend in the dry signal when used as an insert"),
                       knob ("level", "LEVEL", "Effect return level") }, {}, false, Section::Extra };
        }

        void prepareDsp (double sampleRate, int maxBlock) override
        {
            mixS.prepare (sampleRate, 20.0f);
            levelS.prepare (sampleRate, 20.0f);
            mixS.reset (pv ("mix") * 0.01f);
            levelS.reset (dbToGain (pv ("level")));
            prepareFx (sampleRate, maxBlock);
        }

        void resetDsp() override { resetFx(); }

        void processDsp (juce::AudioBuffer<float>& b) override
        {
            beginBlock();
            mixS.setTarget (pv ("mix") * 0.01f);
            levelS.setTarget (dbToGain (pv ("level")));
            auto* l = b.getWritePointer (0);
            auto* r = b.getWritePointer (1);
            for (int i = 0, n = b.getNumSamples(); i < n; ++i)
            {
                float wl = 0.0f, wr = 0.0f;
                processFrame (l[i], r[i], wl, wr);
                const float m = mixS.next(), g = levelS.next();
                l[i] = l[i] * (1.0f - m) + wl * m * g;
                r[i] = r[i] * (1.0f - m) + wr * m * g;
            }
        }

        static juce::StringArray noteNamesPublic() { return noteNames(); }
        static float noteBeatsPublic (int idx) { return noteBeats (idx); }

    protected:
        virtual void prepareFx (double sampleRate, int maxBlock) = 0;
        virtual void resetFx() = 0;
        virtual void beginBlock() {}
        virtual void processFrame (float inL, float inR, float& wetL, float& wetR) = 0;

        // shared delay-time helpers for the tempo-synced delays
        static juce::StringArray noteNames()
        {
            return { "1/1", "1/2 D", "1/1 T", "1/2", "1/4 D", "1/2 T", "1/4", "1/8 D", "1/4 T", "1/8", "1/16 D", "1/8 T", "1/16", "1/16 T" };
        }
        static float noteBeats (int idx)   // in quarter notes
        {
            static const float beats[] = { 4.0f, 3.0f, 8.0f / 3.0f, 2.0f, 1.5f, 4.0f / 3.0f, 1.0f, 0.75f, 2.0f / 3.0f, 0.5f, 0.375f, 1.0f / 3.0f, 0.25f, 1.0f / 6.0f };
            return beats[juce::jlimit (0, 13, idx)];
        }

    private:
        Smoother mixS, levelS;
    };
}
