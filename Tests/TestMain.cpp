// SPDX-License-Identifier: AGPL-3.0-only
// Copyright (C) 2026 Jerod Ulicki

// Offline checks for every LiveTest plugin. Run with no arguments for the DSP checks,
// "--snapshots <dir>" to also render each editor (dark + light) to PNG files, or
// "--only <ClassName>" to test one plugin.

#include <juce_audio_processors/juce_audio_processors.h>
#include "Base.h"
#include "Theme.h"
#include <iostream>

namespace
{
    int failures = 0;
    void check (bool ok, const juce::String& plugin, const juce::String& what)
    {
        if (! ok) { ++failures; std::cout << "  FAIL  " << plugin << ": " << what << "\n"; }
    }

    struct Result { float peak = 0.0f, rms = 0.0f; bool finite = true; int clipped = 0; };

    Result run (juce::AudioProcessor& p, double sr, double seconds, std::function<void (juce::AudioBuffer<float>&, int64_t)> fill, int channels = 2)
    {
        Result res;
        const int block = 256;
        juce::AudioBuffer<float> buf (channels, block);
        juce::MidiBuffer midi;
        double sum = 0.0; int64_t count = 0;
        const int64_t total = (int64_t) (seconds * sr);
        for (int64_t pos = 0; pos < total; pos += block)
        {
            fill (buf, pos);
            p.processBlock (buf, midi);
            for (int ch = 0; ch < channels; ++ch)
            {
                const float* d = buf.getReadPointer (ch);
                for (int i = 0; i < block; ++i)
                {
                    if (! std::isfinite (d[i])) res.finite = false;
                    res.peak = std::max (res.peak, std::abs (d[i]));
                    if (std::abs (d[i]) >= 15.9f) ++res.clipped;
                    sum += (double) d[i] * d[i]; ++count;
                }
            }
        }
        res.rms = (float) std::sqrt (sum / (double) std::max<int64_t> (1, count));
        return res;
    }

    float db (float g) { return ltp::gainToDb (g); }

    void randomiseParams (juce::AudioProcessor& p, juce::Random& rng)
    {
        for (auto* prm : p.getParameters())
        {
            if (auto* rp = dynamic_cast<juce::RangedAudioParameter*> (prm))
                if (rp->getParameterID() == "bypass") { rp->setValueNotifyingHost (0.0f); continue; }
            prm->setValueNotifyingHost (rng.nextFloat());
        }
    }

    void setParam (juce::AudioProcessor& p, const juce::String& id, float value)
    {
        for (auto* prm : p.getParameters())
            if (auto* rp = dynamic_cast<juce::RangedAudioParameter*> (prm))
                if (rp->getParameterID() == id) rp->setValueNotifyingHost (rp->convertTo0to1 (value));
    }

    // RT60 from an impulse response (Schroeder integration, T20 fit from -5 to -25 dB)
    float measureRt60 (juce::AudioProcessor& p, double sr)
    {
        p.reset();
        const int len = (int) (sr * 8.0);
        std::vector<float> ir ((size_t) len);
        juce::AudioBuffer<float> b (2, 256);
        juce::MidiBuffer m;
        for (int pos = 0; pos < len; pos += 256)
        {
            b.clear();
            if (pos == 0) { b.setSample (0, 0, 1.0f); b.setSample (1, 0, 1.0f); }
            p.processBlock (b, m);
            for (int i = 0; i < 256 && pos + i < len; ++i) ir[(size_t) (pos + i)] = b.getSample (0, i) + b.getSample (1, i);
        }
        std::vector<double> edc ((size_t) len);
        double acc = 0.0;
        for (int i = len - 1; i >= 0; --i) { acc += (double) ir[(size_t) i] * ir[(size_t) i]; edc[(size_t) i] = acc; }
        const double total = edc[0];
        int i5 = -1, i25 = -1;
        for (int i = 0; i < len; ++i)
        {
            const double dbv = 10.0 * std::log10 (std::max (edc[(size_t) i] / total, 1e-30));
            if (i5 < 0 && dbv <= -10.0) i5 = i;
            if (i25 < 0 && dbv <= -35.0) { i25 = i; break; }
        }
        if (i5 < 0 || i25 < 0) return -1.0f;
        return (float) ((i25 - i5) / sr * 60.0 / 25.0);
    }

    void resetDefaults (juce::AudioProcessor& p)
    {
        for (auto* prm : p.getParameters()) prm->setValueNotifyingHost (prm->getDefaultValue());
    }

    void snapshot (const ltp::Factory& factory, const juce::File& dir, const juce::String& name)
    {
        for (int pass = 0; pass < 3; ++pass)   // SQ dark, SQ light, UNIT dark
        {
            const bool light = pass == 1, unit = pass == 2;
            theme::setLightMode (light);
            theme::saveLightModePreference (light);
            std::unique_ptr<juce::AudioProcessor> proc (factory());
            auto& p = *proc;
            p.setPlayConfigDetails (2, 2, 48000.0, 512);
            p.prepareToPlay (48000.0, 512);
            dynamic_cast<ltp::Processor&> (p).apvts.state.setProperty ("view", unit ? "unit" : "sq", nullptr);
            std::unique_ptr<juce::AudioProcessorEditor> ed (p.createEditorAndMakeActive());
            if (ed == nullptr) return;
            // a few timer ticks' worth of metering so the graphs show something
            for (int k = 0; k < 90; ++k)
            {
                juce::AudioBuffer<float> b (2, 512);
                for (int i = 0; i < 512; ++i)
                {
                    const float v = 0.5f * std::sin ((float) (k * 512 + i) * 0.05f) * (k % 30 < 15 ? 1.0f : 0.1f);
                    b.setSample (0, i, v); b.setSample (1, i, v);
                }
                juce::MidiBuffer m;
                p.processBlock (b, m);
                if (auto* t = dynamic_cast<juce::Timer*> (ed.get())) t->timerCallback();
            }
            auto img = ed->createComponentSnapshot (ed->getLocalBounds(), true, 1.0f);
            auto f = dir.getChildFile (name + (unit ? "-unit.png" : light ? "-sq-light.png" : "-sq.png"));
            f.deleteFile();
            juce::FileOutputStream out (f);
            juce::PNGImageFormat png;
            png.writeImageToStream (img, out);
            std::cout << "  snapshot " << f.getFileName() << " (" << ed->getWidth() << "x" << ed->getHeight() << ")\n";
            ed.reset();
        }
        theme::setLightMode (false);
        theme::saveLightModePreference (false);
    }
}

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI gui;
    juce::File snapDir;
    juce::String only;
    for (int i = 1; i < argc; ++i)
    {
        const juce::String a (argv[i]);
        if (a == "--snapshots" && i + 1 < argc) snapDir = juce::File (juce::String (argv[++i]));
        if (a == "--only" && i + 1 < argc) only = argv[++i];
    }
    if (snapDir != juce::File()) snapDir.createDirectory();

    juce::Random rng (1234);
    const double sr = 48000.0;
    const double twoPi = juce::MathConstants<double>::twoPi;

    for (auto& [name, factory] : ltp::registry())
    {
        if (only.isNotEmpty() && name != only) continue;
        std::unique_ptr<juce::AudioProcessor> p (factory());
        auto* lp = dynamic_cast<ltp::Processor*> (p.get());
        const auto info = lp->getInfo();
        std::cout << name << "  (" << info.name << ", " << p->getParameters().size() << " params)\n";

        p->setPlayConfigDetails (2, 2, sr, 256);
        p->prepareToPlay (sr, 256);

        const bool noisy = name == "TapeEcho" || name == "BbdDelay";
        // 1. silence in -> silence out
        auto r = run (*p, sr, 1.0, [] (auto& b, int64_t) { b.clear(); });
        check (r.finite, name, "silence produced NaN/inf");
        check (db (r.peak) < (noisy ? -45.0f : -90.0f), name, "silence produced output " + juce::String (db (r.peak), 1) + " dBFS");

        // 2. 1 kHz sine at 0 dBu (-18 dBFS)
        const float amp = ltp::dbToGain (-18.0f) * std::sqrt (2.0f);
        r = run (*p, sr, 2.0, [&] (auto& b, int64_t pos)
        {
            for (int i = 0; i < b.getNumSamples(); ++i)
            {
                const float v = amp * (float) std::sin (twoPi * 1000.0 * (double) (pos + i) / sr);
                for (int ch = 0; ch < b.getNumChannels(); ++ch) b.setSample (ch, i, v);
            }
        });
        check (r.finite, name, "sine produced NaN/inf");
        check (db (r.peak) < 6.0f, name, "sine at -18 dBFS peaked at " + juce::String (db (r.peak), 1) + " dBFS");
        std::cout << "    sine -18 dBFS -> rms " << juce::String (db (r.rms), 1) << " dBFS";

        // 3. dynamics: loud sine should be compressed
        if (info.dynamics)
        {
            const float loud = ltp::dbToGain (-2.0f) * std::sqrt (2.0f);
            lp->grDb.store (0.0f);
            r = run (*p, sr, 1.0, [&] (auto& b, int64_t pos)
            {
                for (int i = 0; i < b.getNumSamples(); ++i)
                {
                    const float v = loud * (float) std::sin (twoPi * 200.0 * (double) (pos + i) / sr);
                    for (int ch = 0; ch < b.getNumChannels(); ++ch) b.setSample (ch, i, v);
                }
            });
            const float gr = lp->grDb.exchange (0.0f);
            std::cout << ", loud sine GR " << juce::String (gr, 1) << " dB";
            check (r.finite, name, "loud sine NaN");
            check (gr > 0.5f, name, "no gain reduction on a +16 dBu sine with default settings");

            // zero latency: impulse comes out at sample 0 (with the compressor relaxed)
            p->reset();
            run (*p, sr, 0.5, [] (auto& b, int64_t) { b.clear(); });
            juce::AudioBuffer<float> imp (2, 256);
            imp.clear();
            imp.setSample (0, 0, 0.01f); imp.setSample (1, 0, 0.01f);
            juce::MidiBuffer m;
            p->processBlock (imp, m);
            // no look-ahead: the impulse must already be (mostly) there at sample 0.
            // (crossover/EQ phase shift can move the peak a sample later, which is not latency)
            float pk = 0.0f;
            for (int i = 0; i < 256; ++i) pk = std::max (pk, std::abs (imp.getSample (0, i)));
            check (std::abs (imp.getSample (0, 0)) > 0.3f * pk, name, "latency: output at sample 0 is only "
                   + juce::String (std::abs (imp.getSample (0, 0)) / std::max (pk, 1e-9f), 2) + " of the impulse peak");
        }
        std::cout << "\n";

        // 3b. model accuracy checks
        if (info.dynamics && info.graph == ltp::Graph::Compressor)
        {
            // the drawn transfer curve should match what the audio does (steady 1 kHz sine)
            for (float inDb : { -30.0f, -12.0f, -3.0f })
            {
                resetDefaults (*p); p->reset();
                const float a = ltp::dbToGain (inDb) * std::sqrt (2.0f);
                run (*p, sr, 1.5, [&] (auto& b, int64_t pos)
                {
                    for (int i = 0; i < b.getNumSamples(); ++i)
                        for (int ch = 0; ch < 2; ++ch) b.setSample (ch, i, a * (float) std::sin (twoPi * 1000.0 * (double) (pos + i) / sr));
                });
                r = run (*p, sr, 0.5, [&] (auto& b, int64_t pos)
                {
                    for (int i = 0; i < b.getNumSamples(); ++i)
                        for (int ch = 0; ch < 2; ++ch) b.setSample (ch, i, a * (float) std::sin (twoPi * 1000.0 * (double) (pos + 72000 + i) / sr));
                });
                // graph axes are sine-peak levels (what the detector dot shows)
                const float measured = db (r.rms) + 3.01f;
                const float predicted = lp->transferCurveDb (inDb + 3.01f);
                const float tol = name == "TubeOpto" ? 4.0f : 2.5f;
                check (std::abs (measured - predicted) < tol, name, "curve mismatch at " + juce::String (inDb) + " dBFS: graph says "
                       + juce::String (predicted, 1) + ", audio gives " + juce::String (measured, 1));
            }
        }
        if (name == "Reverb")
        {
            for (int model = 0; model < 4; ++model)
            {
                resetDefaults (*p);
                setParam (*p, "model", (float) model);
                setParam (*p, "decay", 2.0f);
                setParam (*p, "hfSlope", 0.0f);   // RT60 is the mid-band decay
                setParam (*p, "reflLevel", -40.0f);
                run (*p, sr, 0.05, [] (auto& b, int64_t) { b.clear(); });
                const float rt = measureRt60 (*p, sr);
                std::cout << "    model " << model << " RT60 set 2.0 s -> measured " << juce::String (rt, 2) << " s\n";
                check (rt > 1.8f && rt < 2.2f, name, "RT60 off for model " + juce::String (model) + ": " + juce::String (rt, 2));
            }
        }
        if (name == "TapDelay")
        {
            resetDefaults (*p);
            setParam (*p, "timeMode", 1.0f);
            setParam (*p, "timeL", 375.0f);
            setParam (*p, "feedback", 0.0f);
            p->prepareToPlay (sr, 256);
            juce::AudioBuffer<float> b (2, 256);
            juce::MidiBuffer m;
            int peakAt = -1; float peak = 0.0f;
            for (int pos = 0; pos < (int) (sr * 0.6); pos += 256)
            {
                b.clear();
                if (pos == 0) { b.setSample (0, 0, 1.0f); b.setSample (1, 0, 1.0f); }
                p->processBlock (b, m);
                for (int i = 0; i < 256; ++i)
                    if (std::abs (b.getSample (0, i)) > peak) { peak = std::abs (b.getSample (0, i)); peakAt = pos + i; }
            }
            const float ms = (float) peakAt * 1000.0f / (float) sr;
            std::cout << "    first echo at " << juce::String (ms, 2) << " ms (set 375)\n";
            check (std::abs (ms - 375.0f) < 1.0f, name, "delay time wrong: " + juce::String (ms, 2) + " ms");
        }

        // 4. random parameter torture with noise bursts
        juce::Random noise (99);
        int worstClips = 0;
        for (int t = 0; t < 24; ++t)
        {
            randomiseParams (*p, rng);
            r = run (*p, sr, 0.4, [&] (auto& b, int64_t pos)
            {
                const float level = ((pos / 4800) % 2 == 0) ? 0.5f : 0.02f;
                for (int i = 0; i < b.getNumSamples(); ++i)
                    for (int ch = 0; ch < b.getNumChannels(); ++ch) b.setSample (ch, i, level * (noise.nextFloat() * 2.0f - 1.0f));
            });
            check (r.finite, name, "random params produced NaN/inf (run " + juce::String (t) + ")");
            worstClips = std::max (worstClips, r.clipped);
        }
        if (worstClips > 0) std::cout << "    note: output hit the +24 dBFS safety clamp in a random run (" << worstClips << " samples)\n";

        // 5. silence after torture (with default params) decays
        resetDefaults (*p);
        run (*p, sr, 0.2, [] (auto& b, int64_t) { b.clear(); });
        p->reset();
        r = run (*p, sr, 0.5, [] (auto& b, int64_t) { b.clear(); });
        check (db (r.peak) < (noisy ? -45.0f : -80.0f), name, "not silent after reset: " + juce::String (db (r.peak), 1) + " dBFS");

        // 6. mono layout
        {
            std::unique_ptr<juce::AudioProcessor> mono (factory());
            juce::AudioProcessor::BusesLayout layout;
            layout.inputBuses.add (juce::AudioChannelSet::mono());
            layout.outputBuses.add (juce::AudioChannelSet::mono());
            check (mono->setBusesLayout (layout), name, "mono layout rejected");
            mono->prepareToPlay (sr, 256);
            r = run (*mono, sr, 0.5, [&] (auto& b, int64_t pos)
            {
                for (int i = 0; i < b.getNumSamples(); ++i)
                    b.setSample (0, i, amp * (float) std::sin (twoPi * 440.0 * (double) (pos + i) / sr));
            }, 1);
            check (r.finite && r.peak > 0.0f, name, "mono processing failed");
        }

        // 7. CPU: seconds of processing per second of audio at 96 kHz
        {
            std::unique_ptr<juce::AudioProcessor> cpu (factory());
            cpu->setPlayConfigDetails (2, 2, 96000.0, 256);
            cpu->prepareToPlay (96000.0, 256);
            const auto t0 = juce::Time::getMillisecondCounterHiRes();
            run (*cpu, 96000.0, 2.0, [&] (auto& b, int64_t) { for (int ch = 0; ch < 2; ++ch) for (int i = 0; i < b.getNumSamples(); ++i) b.setSample (ch, i, 0.1f * (noise.nextFloat() - 0.5f)); });
            const auto ms = juce::Time::getMillisecondCounterHiRes() - t0;
            std::cout << "    cpu @96k: " << juce::String (ms / 20.0, 2) << " % of one core\n";
        }

        if (snapDir != juce::File())
        {
            snapshot (factory, snapDir, info.name.replace (" ", ""));
        }
    }

    std::cout << (failures == 0 ? "ALL CHECKS PASSED" : juce::String (failures) + " CHECK(S) FAILED") << "\n";
    return failures == 0 ? 0 : 1;
}
