// SPDX-License-Identifier: AGPL-3.0-only
// Copyright (C) 2026 Jerod Ulicki

#include "Base.h"
#include "Ui.h"

namespace ltp
{
    std::vector<std::pair<juce::String, Factory>>& registry()
    {
        static std::vector<std::pair<juce::String, Factory>> r;
        return r;
    }

    // ================================================================ parameter helpers
    namespace param
    {
        juce::String format (float v, Unit u, int decimals)
        {
            auto num = [] (float x, int d) { return juce::String (x, d); };
            auto signedDb = [&] (float x, int d) { return (x > 0.049f ? "+" : "") + num (x, d); };

            switch (u)
            {
                case Unit::Db:      return signedDb (v, decimals < 0 ? 1 : decimals) + " dB";
                case Unit::Dbu:     return signedDb (v, decimals < 0 ? 1 : decimals) + " dBu";
                case Unit::Ms:
                    if (v >= 1000.0f) return num (v / 1000.0f, 2) + " s";
                    return num (v, decimals >= 0 ? decimals : (v < 1.0f ? 2 : v < 10.0f ? 1 : 0)) + " ms";
                case Unit::Hz:
                    if (v >= 1000.0f) return num (v / 1000.0f, v >= 10000.0f ? 1 : 2) + " kHz";
                    return num (v, decimals >= 0 ? decimals : (v < 10.0f ? 2 : v < 100.0f ? 1 : 0)) + " Hz";
                case Unit::Percent: return num (v, decimals < 0 ? 0 : decimals) + " %";
                case Unit::Ratio:   return v >= 99.5f ? juce::String ("inf:1") : num (v, v < 10.0f ? 1 : 0) + ":1";
                case Unit::Seconds: return num (v, decimals < 0 ? 2 : decimals) + " s";
                case Unit::Semitones: return signedDb (v, decimals < 0 ? 0 : decimals) + " st";
                case Unit::Plain:   return num (v, decimals < 0 ? 1 : decimals);
                case Unit::None:    break;
            }
            return num (v, decimals < 0 ? 2 : decimals);
        }

        static float parse (const juce::String& t, Unit u, float maxV)
        {
            auto s = t.trim().toLowerCase();
            if (u == Unit::Ratio && s.startsWith ("inf")) return maxV;
            float v = s.getFloatValue();
            if (u == Unit::Hz && s.containsChar ('k')) v *= 1000.0f;
            if (u == Unit::Ms && s.endsWith (" s") ) v *= 1000.0f;
            return v;
        }

        static juce::NormalisableRange<float> makeRange (float min, float max, float skewCentre, float step)
        {
            juce::NormalisableRange<float> r (min, max, step);
            if (skewCentre > min && skewCentre < max) r.setSkewForCentre (skewCentre);
            return r;
        }

        void addFloat (Layout& l, const juce::String& id, const juce::String& name, float min, float max, float def,
                       Unit unit, float skewCentre, float step, int decimals)
        {
            auto attrs = juce::AudioParameterFloatAttributes()
                .withStringFromValueFunction ([unit, decimals] (float v, int) { return format (v, unit, decimals); })
                .withValueFromStringFunction ([unit, max] (const juce::String& t) { return parse (t, unit, max); });
            l.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { id, 1 }, name,
                                                                makeRange (min, max, skewCentre, step), def, attrs));
        }

        void addFloatText (Layout& l, const juce::String& id, const juce::String& name, float min, float max, float def,
                           std::function<juce::String (float)> toText, float skewCentre, float step)
        {
            auto attrs = juce::AudioParameterFloatAttributes()
                .withStringFromValueFunction ([toText] (float v, int) { return toText (v); })
                .withValueFromStringFunction ([] (const juce::String& t) { return t.getFloatValue(); });
            l.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { id, 1 }, name,
                                                                makeRange (min, max, skewCentre, step), def, attrs));
        }

        void addChoice (Layout& l, const juce::String& id, const juce::String& name, const juce::StringArray& items, int def)
        {
            l.add (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { id, 1 }, name, items, def));
        }

        void addBool (Layout& l, const juce::String& id, const juce::String& name, bool def)
        {
            l.add (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { id, 1 }, name, def));
        }
    }

    // ================================================================ processor
    Processor::Processor (Layout layout)
        : juce::AudioProcessor (BusesProperties()
                                   .withInput ("Input", juce::AudioChannelSet::stereo(), true)
                                   .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
          apvts (*this, nullptr, "LiveTest", std::move (layout))
    {
        for (auto& b : bandGrDb) b.store (0.0f);
    }

    bool Processor::isBusesLayoutSupported (const BusesLayout& layouts) const
    {
        const auto in = layouts.getMainInputChannelSet(), out = layouts.getMainOutputChannelSet();
        const bool inOk = in == juce::AudioChannelSet::mono() || in == juce::AudioChannelSet::stereo();
        const bool outOk = out == juce::AudioChannelSet::mono() || out == juce::AudioChannelSet::stereo();
        return inOk && outOk && (in == out || in == juce::AudioChannelSet::mono());
    }

    void Processor::prepareToPlay (double sampleRate, int samplesPerBlock)
    {
        sr = sampleRate > 0.0 ? sampleRate : 48000.0;
        const int block = std::max (16, samplesPerBlock);
        work.setSize (2, block, false, true, false);
        dry.setSize (2, block, false, true, false);
        bypassMix.prepare (sr, 15.0f);
        bypassMix.reset (pb ("bypass") ? 0.0f : 1.0f);
        prepareDsp (sr, block);
        resetDsp();
    }

    void Processor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
    {
        juce::ScopedNoDenormals noDenormals;
        const int n = buffer.getNumSamples();
        if (n <= 0) return;
        const int inCh = getTotalNumInputChannels(), outCh = getTotalNumOutputChannels();

        if (work.getNumSamples() < n)   // host sent a bigger block than announced
        {
            work.setSize (2, n, false, true, true);
            dry.setSize (2, n, false, true, true);
        }

        if (auto* ph = getPlayHead())
            if (auto pos = ph->getPosition())
                if (auto bpm = pos->getBpm())
                    if (*bpm > 20.0 && *bpm < 400.0) hostBpm.store (*bpm);

        // up-mix into the 2-channel work buffer
        for (int ch = 0; ch < 2; ++ch)
        {
            if (inCh > 0) work.copyFrom (ch, 0, buffer, std::min (ch, inCh - 1), 0, n);
            else work.clear (ch, 0, n);
            dry.copyFrom (ch, 0, work, ch, 0, n);
        }

        float inPeak = std::max (work.getMagnitude (0, 0, n), work.getMagnitude (1, 0, n));
        { float c = inPeakDb.load(); const float d = gainToDb (inPeak); while (d > c && ! inPeakDb.compare_exchange_weak (c, d)) {} }

        juce::AudioBuffer<float> view (work.getArrayOfWritePointers(), 2, n);
        processDsp (view);

        // safety: never let a NaN/inf (or a runaway feedback loop) reach the speakers
        for (int ch = 0; ch < 2; ++ch)
        {
            auto* d = view.getWritePointer (ch);
            for (int i = 0; i < n; ++i)
                if (! std::isfinite (d[i])) { view.clear(); resetDsp(); break; }
                else d[i] = clampf (d[i], -16.0f, 16.0f);
        }

        // bypass crossfade
        bypassMix.setTarget (pb ("bypass") ? 0.0f : 1.0f);
        if (! bypassMix.settled() || bypassMix.current < 0.999f)
        {
            auto* l = view.getWritePointer (0); auto* r = view.getWritePointer (1);
            const auto* dl = dry.getReadPointer (0); const auto* dr = dry.getReadPointer (1);
            for (int i = 0; i < n; ++i)
            {
                const float m = bypassMix.next();
                l[i] = dl[i] + m * (l[i] - dl[i]);
                r[i] = dr[i] + m * (r[i] - dr[i]);
            }
        }

        float outPeak = std::max (view.getMagnitude (0, 0, n), view.getMagnitude (1, 0, n));
        { float c = outPeakDb.load(); const float d = gainToDb (outPeak); while (d > c && ! outPeakDb.compare_exchange_weak (c, d)) {} }

        // down-mix to the host's output layout
        if (outCh == 1)
        {
            buffer.copyFrom (0, 0, view, 0, 0, n);
            buffer.addFrom (0, 0, view, 1, 0, n);
            buffer.applyGain (0, 0, n, 0.5f);
        }
        else
        {
            for (int ch = 0; ch < outCh; ++ch)
                if (ch < 2) buffer.copyFrom (ch, 0, view, ch, 0, n);
                else buffer.clear (ch, 0, n);
        }
    }

    juce::AudioProcessorEditor* Processor::createEditor() { return new Editor (*this); }

    void Processor::getStateInformation (juce::MemoryBlock& dest)
    {
        auto state = apvts.copyState();
        state.setProperty ("pluginVersion", LTP_VERSION, nullptr);
        if (auto xml = state.createXml()) copyXmlToBinary (*xml, dest);
    }

    void Processor::setStateInformation (const void* data, int size)
    {
        if (auto xml = getXmlFromBinary (data, size))
            if (xml->hasTagName (apvts.state.getType()))
                apvts.replaceState (juce::ValueTree::fromXml (*xml));
    }

    void Processor::tap()
    {
        const double now = juce::Time::getMillisecondCounterHiRes();
        const double gap = now - lastTapMs;
        lastTapMs = now;
        if (gap > 2500.0 || gap < 60.0) { tapIntervals.clear(); return; }
        tapIntervals.push_back (gap);
        if (tapIntervals.size() > 4) tapIntervals.erase (tapIntervals.begin());
        double avg = 0.0;
        for (auto v : tapIntervals) avg += v;
        avg /= (double) tapIntervals.size();

        auto set = [this] (const char* id, float value)
        {
            if (auto* p = apvts.getParameter (id))
            {
                p->beginChangeGesture();
                p->setValueNotifyingHost (p->convertTo0to1 (value));
                p->endChangeGesture();
            }
        };

        const bool msMode = apvts.getParameter ("timeMode") != nullptr && pi ("timeMode") == 1;
        if (! msMode && apvts.getParameter ("bpm") != nullptr) set ("bpm", (float) (60000.0 / avg));
        else if (apvts.getParameter ("timeL") != nullptr) set ("timeL", (float) avg);
    }
}
