// SPDX-License-Identifier: AGPL-3.0-only
// Copyright (C) 2026 Jerod Ulicki

#include "Faceplate.h"
#include "Theme.h"

namespace ltp
{
    namespace
    {
        using C = juce::Colour;

        juce::Font hwFont (float size, bool bold = true) { return theme::font (size, bold); }

        // ------------------------------------------------------------ needle VU meter
        class VuMeter : public juce::Component
        {
        public:
            explicit VuMeter (juce::String l) : label (std::move (l)) { setOpaque (true); }
            void setGr (float gr)
            {
                const float target = clampf (-gr, -20.0f, 3.0f);
                value += (target - value) * 0.3f;
                if (std::abs (value - drawn) > 0.02f) { drawn = value; repaint(); }
            }
            static float posFor (float db)   // VU scale is linear in voltage
            {
                const float lo = std::pow (10.0f, -20.0f / 20.0f), hi = std::pow (10.0f, 3.0f / 20.0f);
                return (std::pow (10.0f, db / 20.0f) - lo) / (hi - lo);
            }
            void paint (juce::Graphics& g) override
            {
                auto r = getLocalBounds().toFloat();
                g.setColour (C (0xff15130f));
                g.fillRoundedRectangle (r, 6.0f);
                auto face = r.reduced (7.0f);
                juce::ColourGradient grad (C (0xfff6ebcb), face.getCentreX(), face.getY(), C (0xffd9c79a), face.getCentreX(), face.getBottom(), false);
                g.setGradientFill (grad);
                g.fillRoundedRectangle (face, 3.0f);

                const juce::Point<float> pivot (face.getCentreX(), face.getBottom() + face.getHeight() * 0.35f);
                const float radius = face.getHeight() * 0.98f;
                const float span = 0.68f;   // radians either side
                auto angleFor = [&] (float db) { return -span + 2.0f * span * posFor (db); };
                auto pt = [&] (float a, float rad) { return juce::Point<float> (pivot.x + rad * std::sin (a), pivot.y - rad * std::cos (a)); };

                g.saveState();
                g.reduceClipRegion (face.toNearestInt());
                // red zone
                juce::Path red;
                red.addCentredArc (pivot.x, pivot.y, radius * 0.86f, radius * 0.86f, 0.0f, angleFor (0.0f), angleFor (3.0f), true);
                g.setColour (C (0xffc0322b));
                g.strokePath (red, juce::PathStrokeType (4.0f));
                juce::Path arc;
                arc.addCentredArc (pivot.x, pivot.y, radius * 0.86f, radius * 0.86f, 0.0f, angleFor (-20.0f), angleFor (0.0f), true);
                g.setColour (C (0xff2b2618));
                g.strokePath (arc, juce::PathStrokeType (1.4f));

                g.setFont (hwFont (std::max (8.5f, face.getHeight() * 0.085f), false));
                for (float db : { -20.0f, -10.0f, -7.0f, -5.0f, -3.0f, -2.0f, -1.0f, 0.0f, 1.0f, 2.0f, 3.0f })
                {
                    const float a = angleFor (db);
                    g.setColour (db > 0.0f ? C (0xffc0322b) : C (0xff2b2618));
                    g.drawLine ({ pt (a, radius * 0.86f), pt (a, radius * 0.93f) }, 1.4f);
                    const auto tp = pt (a, radius * 1.0f);
                    const juce::String t = db > 0.0f ? "+" + juce::String ((int) db) : juce::String ((int) db);
                    g.drawText (t, juce::Rectangle<float> (tp.x - 14.0f, tp.y - 7.0f, 28.0f, 14.0f), juce::Justification::centred);
                }
                g.setColour (C (0xff2b2618));
                g.setFont (hwFont (std::max (9.0f, face.getHeight() * 0.1f)));
                g.drawText (label, face.withTrimmedTop (face.getHeight() * 0.5f).withTrimmedBottom (face.getHeight() * 0.12f), juce::Justification::centred);

                // needle
                const float a = angleFor (drawn);
                g.setColour (C (0xff111111));
                g.drawLine ({ pt (a, radius * 0.25f), pt (a, radius * 0.97f) }, 1.6f);
                g.restoreState();
                // pivot cover
                g.setColour (C (0xff15130f));
                g.fillRect (face.withTop (face.getBottom() - face.getHeight() * 0.12f));
                g.setColour (C (0xff000000).withAlpha (0.35f));
                g.drawRoundedRectangle (face, 3.0f, 1.5f);
            }
        private:
            juce::String label;
            float value = 0.0f, drawn = 0.0f;
        };

        // ------------------------------------------------------------ gain-reduction LED ladder
        class LedLadder : public juce::Component
        {
        public:
            LedLadder (C p, C i) : panelC (p), inkC (i) { setOpaque (true); }
            void setColours (C p, C i) { panelC = p; inkC = i; repaint(); }
            void setGr (float g) { if (std::abs (g - gr) > 0.1f) { gr = g; repaint(); } }
            void paint (juce::Graphics& g) override
            {
                g.fillAll (panelC);
                static constexpr float steps[] = { 1, 2, 3, 4, 6, 8, 10, 12, 16, 20 };
                const float h = (float) getHeight() / 10.0f;
                g.setFont (hwFont (9.0f));
                for (int i = 0; i < 10; ++i)
                {
                    auto led = juce::Rectangle<float> (4.0f, (float) i * h + 2.0f, 14.0f, h - 4.0f);
                    const C on = i < 5 ? C (0xff3be05a) : i < 8 ? C (0xffffc53a) : C (0xffff3b2f);
                    const bool lit = gr >= steps[i] - 0.05f;
                    g.setColour (lit ? on : on.withBrightness (0.22f));
                    g.fillRoundedRectangle (led, 2.0f);
                    if (lit) { g.setColour (on.withAlpha (0.35f)); g.drawRoundedRectangle (led.expanded (1.5f), 3.0f, 2.0f); }
                    g.setColour (inkC);
                    g.drawText (juce::String ((int) steps[i]), juce::Rectangle<float> (20.0f, (float) i * h, (float) getWidth() - 20.0f, h), juce::Justification::centredLeft);
                }
            }
        private:
            C panelC, inkC;
            float gr = 0.0f;
        };

        // ------------------------------------------------------------ lit push button
        class HwButton : public juce::Button
        {
        public:
            HwButton (const juce::String& caption, C ledColour) : juce::Button (caption), led (ledColour) {}
            void paintButton (juce::Graphics& g, bool over, bool down) override
            {
                auto r = getLocalBounds().toFloat().reduced (1.5f);
                g.setColour (C (0xff000000).withAlpha (0.45f));
                g.fillRoundedRectangle (r.translated (0.0f, 2.0f), 3.0f);
                const bool on = getToggleState();
                juce::ColourGradient grad (C (on ? 0xff4a4a4a : 0xff3a3a3a), 0, r.getY(), C (on ? 0xff2a2a2a : 0xff1c1c1c), 0, r.getBottom(), false);
                g.setGradientFill (grad);
                g.fillRoundedRectangle (down ? r.translated (0.0f, 1.0f) : r, 3.0f);
                g.setColour (C (0xff000000));
                g.drawRoundedRectangle (r, 3.0f, 1.0f);
                if (over) { g.setColour (C (0xffffffff).withAlpha (0.06f)); g.fillRoundedRectangle (r, 3.0f); }
                // LED window at the top of the cap
                auto ledR = r.withHeight (5.0f).reduced (r.getWidth() * 0.22f, 0.0f).translated (0.0f, 5.0f);
                g.setColour (on ? led : led.withBrightness (0.2f));
                g.fillRoundedRectangle (ledR, 2.0f);
                if (on) { g.setColour (led.withAlpha (0.3f)); g.fillRoundedRectangle (ledR.expanded (3.0f, 2.0f), 4.0f); }
                g.setColour (C (on ? 0xfff2f2f2 : 0xffbdbdbd));
                g.setFont (hwFont (std::min (11.5f, r.getHeight() * 0.36f)));
                g.drawFittedText (getButtonText(), r.withTrimmedTop (10.0f).toNearestInt(), juce::Justification::centred, 1, 0.7f);
            }
        private:
            C led;
        };

        // ------------------------------------------------------------ bat toggle switch
        class HwSwitch : public juce::Button
        {
        public:
            HwSwitch() : juce::Button ("switch") { setClickingTogglesState (true); }
            void paintButton (juce::Graphics& g, bool, bool) override
            {
                auto r = getLocalBounds().toFloat();
                const auto c = r.getCentre();
                // mounting nut
                g.setColour (C (0xff000000).withAlpha (0.4f));
                g.fillEllipse (c.x - 12.0f, c.y - 10.0f, 24.0f, 24.0f);
                juce::ColourGradient nut (C (0xffe0e0e0), c.x, c.y - 11.0f, C (0xff7a7a7a), c.x, c.y + 11.0f, false);
                g.setGradientFill (nut);
                g.fillEllipse (c.x - 11.0f, c.y - 11.0f, 22.0f, 22.0f);
                g.setColour (C (0xff2a2a2a));
                g.fillEllipse (c.x - 5.0f, c.y - 5.0f, 10.0f, 10.0f);
                // lever
                const bool up = getToggleState();
                const float len = r.getHeight() * 0.42f;
                const juce::Point<float> tip (c.x, up ? c.y - len : c.y + len);
                juce::Path lever;
                lever.startNewSubPath (c);
                lever.lineTo (tip);
                g.setColour (C (0xff000000).withAlpha (0.35f));
                g.strokePath (lever, juce::PathStrokeType (7.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded), juce::AffineTransform::translation (1.5f, 2.0f));
                juce::ColourGradient chrome (C (0xfff4f4f4), c.x - 3.0f, 0, C (0xff8c8c8c), c.x + 3.0f, 0, false);
                g.setGradientFill (chrome);
                g.strokePath (lever, juce::PathStrokeType (6.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
                g.fillEllipse (tip.x - 5.0f, tip.y - 5.0f, 10.0f, 10.0f);
            }
        };

        // ------------------------------------------------------------ value display
        class LcdDisplay : public juce::Component
        {
        public:
            LcdDisplay (juce::RangedAudioParameter* p, C ledColour) : param (p), led (ledColour) { setOpaque (true); }
            void refresh()
            {
                const auto t = param != nullptr ? param->getCurrentValueAsText() : juce::String();
                if (t != text) { text = t; repaint(); }
            }
            void paint (juce::Graphics& g) override
            {
                auto r = getLocalBounds().toFloat();
                g.setColour (C (0xff0b0b0b));
                g.fillRoundedRectangle (r, 3.0f);
                g.setColour (C (0xff000000));
                g.drawRoundedRectangle (r.reduced (0.5f), 3.0f, 1.0f);
                g.setFont (juce::Font (juce::FontOptions (juce::Font::getDefaultMonospacedFontName(), r.getHeight() * 0.55f, juce::Font::bold)));
                g.setColour (led.withAlpha (0.25f));
                g.drawText (text.toUpperCase(), r.reduced (8.0f, 0.0f).translated (0.0f, 1.0f), juce::Justification::centredRight);   // glow
                g.setColour (led);
                g.drawText (text.toUpperCase(), r.reduced (8.0f, 0.0f), juce::Justification::centredRight);
            }
        private:
            juce::RangedAudioParameter* param;
            C led;
            juce::String text;
        };

        // "-46.0 dBu" -> "-46", "1.20 kHz" -> "1.2k", "3.0:1" -> "3", "inf:1" -> "inf"
        juce::String shortLabel (const juce::String& full)
        {
            auto t = full.upToFirstOccurrenceOf (" ", false, false).trim();
            if (t.endsWith (":1")) t = t.dropLastCharacters (2);
            if (t.startsWithIgnoreCase ("inf")) return "inf";
            if (! t.containsOnly ("0123456789.-+")) return t;
            const float v = t.getFloatValue();
            juce::String out = std::abs (v) >= 10.0f ? juce::String (juce::roundToInt (v)) : juce::String (v, 1);
            if (out.endsWith (".0")) out = out.dropLastCharacters (2);
            if (t.startsWith ("+") && v > 0.0f) out = "+" + out;
            return full.contains ("kHz") ? out + "k" : out;
        }

        juce::String abbreviate (const juce::String& s)
        {
            return s.length() <= 7 ? s : s.substring (0, 6) + ".";
        }
    }

    // ================================================================ knob look
    HwLookAndFeel::HwLookAndFeel()
    {
        setColour (juce::BubbleComponent::backgroundColourId, C (0xf0101010));
        setColour (juce::BubbleComponent::outlineColourId, C (0xff6a6a6a));
        setColour (juce::TooltipWindow::textColourId, C (0xffeeeeee));
    }

    juce::Font HwLookAndFeel::getSliderPopupFont (juce::Slider&) { return hwFont (13.0f); }
    int HwLookAndFeel::getSliderPopupPlacement (juce::Slider&) { return juce::BubbleComponent::above; }

    void HwLookAndFeel::drawRotarySlider (juce::Graphics& g, int x, int y, int w, int h, float pos, float start, float end, juce::Slider& s)
    {
        const auto style = (Faceplate::KnobStyle) (int) s.getProperties().getWithDefault ("knob", 0);
        auto b = juce::Rectangle<int> (x, y, w, h).toFloat();
        const float R = std::min (b.getWidth(), b.getHeight()) * 0.5f - 2.0f;
        const auto c = b.getCentre();
        const float a = start + pos * (end - start);
        auto pt = [&] (float rad) { return juce::Point<float> (c.x + rad * std::sin (a), c.y - rad * std::cos (a)); };
        auto circle = [&] (float rad) { return juce::Rectangle<float> (c.x - rad, c.y - rad, rad * 2.0f, rad * 2.0f); };

        // drop shadow
        g.setColour (C (0xff000000).withAlpha (0.42f));
        g.fillEllipse (circle (R).translated (1.5f, 3.0f));

        switch (style)
        {
            case Faceplate::Silver:
            {
                juce::ColourGradient m (C (0xfff2f2f2), c.x - R, c.y - R, C (0xff8e8e8e), c.x + R, c.y + R, false);
                m.addColour (0.5, C (0xffcfcfcf));
                g.setGradientFill (m);
                g.fillEllipse (circle (R));
                g.setColour (C (0xff000000).withAlpha (0.08f));
                for (float k = 0.2f; k < 1.0f; k += 0.12f) g.drawEllipse (circle (R * k), 0.8f);
                g.setColour (C (0xff5a5a5a));
                g.drawEllipse (circle (R), 1.2f);
                g.setColour (C (0xff111111));
                g.drawLine ({ pt (R * 0.3f), pt (R * 0.9f) }, std::max (2.0f, R * 0.09f));
                break;
            }
            case Faceplate::ChickenHead:
            {
                g.setColour (C (0xff1b1b1b));
                g.fillEllipse (circle (R * 0.58f));
                juce::Path head;
                const float wBase = R * 0.36f;
                head.startNewSubPath (0.0f, -R * 0.96f);
                head.quadraticTo (wBase, -R * 0.2f, wBase * 0.9f, R * 0.35f);
                head.quadraticTo (0.0f, R * 0.62f, -wBase * 0.9f, R * 0.35f);
                head.quadraticTo (-wBase, -R * 0.2f, 0.0f, -R * 0.96f);
                head.closeSubPath();
                const auto tr = juce::AffineTransform::rotation (a).translated (c.x, c.y);
                juce::ColourGradient hg (C (0xff3c3c3c), c.x - R, c.y - R, C (0xff0c0c0c), c.x + R, c.y + R, false);
                g.setGradientFill (hg);
                g.fillPath (head, tr);
                g.setColour (C (0xff000000));
                g.strokePath (head, juce::PathStrokeType (1.0f), tr);
                g.setColour (C (0xffeeeeee));
                g.drawLine ({ pt (R * 0.35f), pt (R * 0.86f) }, std::max (1.6f, R * 0.06f));
                break;
            }
            case Faceplate::Cream:
            {
                juce::ColourGradient m (C (0xfffbf4e2), c.x - R, c.y - R, C (0xffcbbd9a), c.x + R, c.y + R, false);
                g.setGradientFill (m);
                g.fillEllipse (circle (R));
                g.setColour (C (0xff8f8264));
                g.drawEllipse (circle (R), 1.2f);
                g.setColour (C (0xffffffff).withAlpha (0.5f));
                g.fillEllipse (circle (R * 0.55f).translated (-R * 0.12f, -R * 0.15f));
                g.setColour (C (0xff2d2a24));
                g.drawLine ({ pt (R * 0.25f), pt (R * 0.92f) }, std::max (2.0f, R * 0.08f));
                break;
            }
            case Faceplate::Pointer:
            {
                juce::ColourGradient m (C (0xff2e2e2e), c.x, c.y - R, C (0xff060606), c.x, c.y + R, false);
                g.setGradientFill (m);
                g.fillEllipse (circle (R));
                g.setColour (C (0xff000000));
                g.drawEllipse (circle (R), 1.0f);
                juce::ColourGradient cap (C (0xffdedede), c.x, c.y - R * 0.4f, C (0xff7d7d7d), c.x, c.y + R * 0.4f, false);
                g.setGradientFill (cap);
                g.fillEllipse (circle (R * 0.42f));
                const auto d = pt (R * 0.78f);
                g.setColour (C (0xfff2f2f2));
                g.fillEllipse (d.x - R * 0.08f, d.y - R * 0.08f, R * 0.16f, R * 0.16f);
                break;
            }
            case Faceplate::BlackSkirt:
            default:
            {
                juce::ColourGradient skirt (C (0xff2c2c2c), c.x, c.y - R, C (0xff0a0a0a), c.x, c.y + R, false);
                g.setGradientFill (skirt);
                g.fillEllipse (circle (R));
                g.setColour (C (0xff000000));
                g.drawEllipse (circle (R), 1.0f);
                // knurled cap
                const float capR = R * 0.66f;
                juce::ColourGradient cap (C (0xff3d3d3d), c.x, c.y - capR, C (0xff141414), c.x, c.y + capR, false);
                g.setGradientFill (cap);
                g.fillEllipse (circle (capR));
                g.setColour (C (0xff000000).withAlpha (0.55f));
                for (int k = 0; k < 28; ++k)
                {
                    const float ka = juce::MathConstants<float>::twoPi * (float) k / 28.0f;
                    g.drawLine (c.x + capR * 0.86f * std::sin (ka), c.y - capR * 0.86f * std::cos (ka),
                                c.x + capR * std::sin (ka), c.y - capR * std::cos (ka), 1.0f);
                }
                g.setColour (C (0xffffffff).withAlpha (0.07f));
                g.fillEllipse (circle (capR * 0.7f).translated (-capR * 0.15f, -capR * 0.2f));
                // pointer on the skirt
                g.setColour (C (0xfff4f4f4));
                g.drawLine ({ pt (capR * 0.2f), pt (R * 0.95f) }, std::max (2.0f, R * 0.07f));
                break;
            }
        }
        if (! s.isEnabled()) { g.setColour (C (0x80000000)); g.fillEllipse (circle (R)); }
    }

    // ================================================================ faceplate
    juce::Point<int> FaceplateView::naturalSize (const FpItem& it, const juce::StringArray& choices)
    {
        static constexpr int knobD[3] = { 40, 56, 78 };
        switch (it.kind)
        {
            case FpItem::Knob:
            case FpItem::Stepped:
            {
                const int d = knobD[juce::jlimit (0, 2, it.size)];
                return { d + 54, 20 + d + 42 };
            }
            case FpItem::Buttons:
            {
                int w = 10;
                for (auto& c : choices) w += std::max (40, juce::GlyphArrangement::getStringWidthInt (hwFont (11.5f), c) + 18) + 6;
                return { w, 74 };
            }
            case FpItem::Push:    return { 78, 74 };
            case FpItem::Switch:  return { 78, 104 };
            case FpItem::Vu:      return it.size >= 2 ? juce::Point<int> { 250, 160 } : juce::Point<int> { 196, 128 };
            case FpItem::Leds:    return { 50, 150 };
            case FpItem::Display: return { 176, 74 };
            case FpItem::Tap:     return { 70, 74 };
            case FpItem::Gap:     return { it.label.isEmpty() ? 22 : 30, 40 };
        }
        return { 60, 60 };
    }

    FaceplateView::FaceplateView (Processor& p, Faceplate s) : proc (p), spec (std::move (s))
    {
        setLookAndFeel (&hwLnf);
        for (auto& row : spec.rows)
            for (auto& it : row)
            {
                Placed pl;
                pl.item = it;
                auto* rp = it.param.isNotEmpty() ? proc.apvts.getParameter (it.param) : nullptr;
                if (auto* cp = dynamic_cast<juce::AudioParameterChoice*> (rp)) pl.choices = cp->choices;
                const auto ledC = C (spec.led);

                switch (it.kind)
                {
                    case FpItem::Knob:
                    case FpItem::Stepped:
                    {
                        auto* sl = new juce::Slider (juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::NoTextBox);
                        if (it.kind == FpItem::Stepped)
                        {
                            const int n = std::max (2, pl.choices.size());
                            const float span = juce::jlimit (1.0f, juce::MathConstants<float>::pi * 1.5f, (float) (n - 1) * 0.55f);
                            sl->setRotaryParameters (juce::MathConstants<float>::twoPi - span * 0.5f, juce::MathConstants<float>::twoPi + span * 0.5f, true);
                            sl->setMouseDragSensitivity (40 * n);
                        }
                        else
                        {
                            sl->setRotaryParameters (juce::MathConstants<float>::pi * 1.25f, juce::MathConstants<float>::pi * 2.75f, true);
                            sl->setMouseDragSensitivity (240);
                        }
                        sl->setPopupDisplayEnabled (true, true, this, 1500);
                        if (rp != nullptr)
                        {
                            sliderAtt.push_back (std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (proc.apvts, it.param, *sl));
                            sl->setDoubleClickReturnValue (true, rp->convertFrom0to1 (rp->getDefaultValue()));
                        }
                        pl.main = sl;
                        break;
                    }
                    case FpItem::Buttons:
                    {
                        for (int k = 0; k < pl.choices.size(); ++k)
                        {
                            auto* b = new HwButton (pl.choices[k], ledC);
                            b->onClick = [this, id = it.param, k]
                            {
                                if (auto* prm = proc.apvts.getParameter (id))
                                {
                                    prm->beginChangeGesture();
                                    prm->setValueNotifyingHost (prm->convertTo0to1 ((float) k));
                                    prm->endChangeGesture();
                                }
                            };
                            owned.add (b);
                            addAndMakeVisible (b);
                            pl.group.push_back (b);
                        }
                        if (! listened.contains (it.param)) { proc.apvts.addParameterListener (it.param, this); listened.add (it.param); }
                        break;
                    }
                    case FpItem::Push:
                    {
                        auto* b = new HwButton ({}, ledC);   // plain lit cap; the label is printed above
                        b->setClickingTogglesState (true);
                        if (rp != nullptr) buttonAtt.push_back (std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (proc.apvts, it.param, *b));
                        pl.main = b;
                        break;
                    }
                    case FpItem::Switch:
                    {
                        auto* sw = new HwSwitch();
                        if (rp != nullptr) buttonAtt.push_back (std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (proc.apvts, it.param, *sw));
                        pl.main = sw;
                        break;
                    }
                    case FpItem::Vu:      pl.main = new VuMeter (it.label); break;
                    case FpItem::Leds:    pl.main = new LedLadder (C (spec.panel), C (spec.ink)); break;
                    case FpItem::Display: pl.main = new LcdDisplay (rp, ledC); break;
                    case FpItem::Tap:
                    {
                        auto* b = new HwButton ("TAP", ledC);
                        b->onClick = [this] { proc.tap(); };
                        pl.main = b;
                        break;
                    }
                    case FpItem::Gap: break;
                }
                if (pl.main != nullptr) { owned.add (pl.main); addAndMakeVisible (pl.main); }
                placed.push_back (pl);
            }

        if (spec.altParam.isNotEmpty() && ! listened.contains (spec.altParam))
        {
            proc.apvts.addParameterListener (spec.altParam, this);
            listened.add (spec.altParam);
        }
        applyLook();
        layout();
        handleAsyncUpdate();
    }

    FaceplateView::~FaceplateView()
    {
        cancelPendingUpdate();
        for (auto& id : listened) proc.apvts.removeParameterListener (id, this);
        sliderAtt.clear();
        buttonAtt.clear();
        setLookAndFeel (nullptr);
    }

    void FaceplateView::applyLook()
    {
        altActive = spec.altParam.isNotEmpty() && spec.altPanel != 0
                    && std::lround (proc.apvts.getRawParameterValue (spec.altParam)->load()) == 1;
        panel = C (altActive ? spec.altPanel : spec.panel);
        ink = C (altActive ? spec.altInk : spec.ink);
        finish = altActive ? spec.altFinish : spec.finish;
        knobs = altActive ? spec.altKnobs : spec.knobs;
        for (auto& pl : placed)
        {
            if (auto* sl = dynamic_cast<juce::Slider*> (pl.main)) { sl->getProperties().set ("knob", (int) knobs); sl->repaint(); }
            if (auto* led = dynamic_cast<LedLadder*> (pl.main)) led->setColours (panel, ink);
        }
    }

    void FaceplateView::handleAsyncUpdate()
    {
        for (auto& pl : placed)
            if (! pl.group.empty())
            {
                const int idx = (int) std::lround (proc.apvts.getRawParameterValue (pl.item.param)->load());
                for (int k = 0; k < (int) pl.group.size(); ++k) pl.group[(size_t) k]->setToggleState (k == idx, juce::dontSendNotification);
            }
        const bool wasAlt = altActive;
        if (spec.altParam.isNotEmpty())
        {
            applyLook();
            if (wasAlt != altActive) { renderBackground(); repaint(); }
        }
    }

    void FaceplateView::layout()
    {
        // measure
        int maxRow = 0, total = titleH;
        size_t idx = 0;
        std::vector<int> rowH;
        for (auto& row : spec.rows)
        {
            int w = 0, h = 0;
            for (size_t k = 0; k < row.size(); ++k, ++idx)
            {
                const auto sz = naturalSize (placed[idx].item, placed[idx].choices);
                w += sz.x + (k > 0 ? itemGap : 0);
                h = std::max (h, sz.y);
            }
            maxRow = std::max (maxRow, w);
            rowH.push_back (h);
            total += h + rowGap;
        }
        prefW = std::max (prefW, maxRow + 2 * ear + 40);
        prefH = total + 14;
        juce::ignoreUnused (rowH);
    }

    void FaceplateView::resized()
    {
        const int inner = getWidth() - 2 * ear - 40;
        int y = titleH;
        size_t idx = 0;
        for (auto& row : spec.rows)
        {
            int w = 0, h = 0;
            for (size_t k = 0; k < row.size(); ++k)
            {
                const auto sz = naturalSize (placed[idx + k].item, placed[idx + k].choices);
                w += sz.x + (k > 0 ? itemGap : 0);
                h = std::max (h, sz.y);
            }
            // spread items across the width a little when there is room
            const int extra = row.size() > 1 ? std::min (40, std::max (0, inner - w) / (int) (row.size() - 1)) : 0;
            const int used = w + extra * (int) (row.size() > 1 ? row.size() - 1 : 0);
            int x = ear + 20 + std::max (0, (inner - used) / 2);
            for (size_t k = 0; k < row.size(); ++k, ++idx)
            {
                auto& pl = placed[idx];
                const auto sz = naturalSize (pl.item, pl.choices);
                pl.area = { x, y + (h - sz.y) / 2, sz.x, sz.y };
                auto a = pl.area;
                switch (pl.item.kind)
                {
                    case FpItem::Knob:
                    case FpItem::Stepped:
                    {
                        static constexpr int knobD[3] = { 40, 56, 78 };
                        const int d = knobD[juce::jlimit (0, 2, pl.item.size)];
                        pl.main->setBounds (a.getCentreX() - d / 2, a.getY() + 20 + 21, d, d);
                        break;
                    }
                    case FpItem::Buttons:
                    {
                        int bx = a.getX() + 5;
                        for (size_t b = 0; b < pl.group.size(); ++b)
                        {
                            const int bw = std::max (40, juce::GlyphArrangement::getStringWidthInt (hwFont (11.5f), pl.choices[(int) b]) + 18);
                            pl.group[b]->setBounds (bx, a.getY() + 24, bw, 42);
                            bx += bw + 6;
                        }
                        break;
                    }
                    case FpItem::Push:
                    case FpItem::Tap:     pl.main->setBounds (a.getCentreX() - 28, a.getY() + 24, 56, 42); break;
                    case FpItem::Switch:  pl.main->setBounds (a.getCentreX() - 16, a.getY() + 22, 32, 60); break;
                    case FpItem::Vu:      pl.main->setBounds (a); break;
                    case FpItem::Leds:    pl.main->setBounds (a.getX() + 6, a.getY() + 20, a.getWidth() - 6, a.getHeight() - 22); break;
                    case FpItem::Display: pl.main->setBounds (a.getX(), a.getY() + 22, a.getWidth(), 42); break;
                    case FpItem::Gap:     break;
                }
                x += sz.x + itemGap + extra;
            }
            y += h + rowGap;
        }
        renderBackground();
    }

    void FaceplateView::renderBackground()
    {
        if (getWidth() <= 0 || getHeight() <= 0) return;
        bg = juce::Image (juce::Image::ARGB, getWidth(), getHeight(), true);
        juce::Graphics g (bg);
        auto r = getLocalBounds().toFloat();

        // panel
        g.setColour (panel);
        g.fillRoundedRectangle (r, 6.0f);
        juce::ColourGradient shade (C (0xffffffff).withAlpha (0.07f), 0, 0, C (0xff000000).withAlpha (0.18f), 0, r.getBottom(), false);
        g.setGradientFill (shade);
        g.fillRoundedRectangle (r, 6.0f);
        if (finish == Faceplate::Brushed)
        {
            juce::Random rng (7);
            for (int yy = 0; yy < getHeight(); yy += 2)
            {
                g.setColour ((rng.nextBool() ? C (0xffffffff) : C (0xff000000)).withAlpha (0.025f + 0.03f * rng.nextFloat()));
                g.drawHorizontalLine (yy, 0.0f, r.getRight());
            }
        }
        else if (finish == Faceplate::Hammertone)
        {
            juce::Random rng (11);
            for (int k = 0; k < getWidth() * getHeight() / 90; ++k)
            {
                const float px = rng.nextFloat() * r.getWidth(), py = rng.nextFloat() * r.getHeight(), d = 2.0f + rng.nextFloat() * 5.0f;
                g.setColour ((rng.nextBool() ? C (0xffffffff) : C (0xff000000)).withAlpha (0.04f));
                g.fillEllipse (px, py, d, d);
            }
        }
        // rack ears with slotted screws
        for (int side = 0; side < 2; ++side)
        {
            const float ex = side == 0 ? 0.0f : r.getWidth() - (float) ear;
            auto e = juce::Rectangle<float> (ex, 0.0f, (float) ear, r.getHeight());
            g.setColour (C (0xff000000).withAlpha (0.12f));
            g.fillRect (e);
            g.setColour (C (0xff000000).withAlpha (0.25f));
            g.drawVerticalLine ((int) (side == 0 ? (float) ear : ex), 4.0f, r.getBottom() - 4.0f);
            for (float sy : { 18.0f, r.getHeight() - 18.0f })
            {
                const float sx = e.getCentreX();
                juce::ColourGradient screw (C (0xffd8d8d8), sx - 6, sy - 6, C (0xff6c6c6c), sx + 6, sy + 6, false);
                g.setGradientFill (screw);
                g.fillEllipse (sx - 6.0f, sy - 6.0f, 12.0f, 12.0f);
                g.setColour (C (0xff2a2a2a));
                g.drawLine (sx - 4.0f, sy + 1.5f, sx + 4.0f, sy - 1.5f, 1.6f);
            }
        }
        g.setColour (C (0xff000000).withAlpha (0.4f));
        g.drawRoundedRectangle (r.reduced (0.5f), 6.0f, 1.0f);

        // title
        g.setColour (ink);
        g.setFont (hwFont (17.0f));
        g.drawText (spec.title, juce::Rectangle<float> ((float) ear + 18.0f, 8.0f, r.getWidth() * 0.6f, 22.0f), juce::Justification::centredLeft);
        g.setColour (ink.withAlpha (0.7f));
        g.setFont (hwFont (10.5f, false));
        g.drawText (spec.subtitle, juce::Rectangle<float> ((float) ear + 19.0f, 28.0f, r.getWidth() * 0.6f, 14.0f), juce::Justification::centredLeft);
        g.setFont (hwFont (11.0f));
        g.drawText ("LIVETEST", juce::Rectangle<float> (r.getWidth() * 0.5f, 10.0f, r.getWidth() * 0.5f - (float) ear - 18.0f, 18.0f), juce::Justification::centredRight);

        // labels and scales
        for (auto& pl : placed)
        {
            const auto a = pl.area.toFloat();
            g.setColour (ink);
            switch (pl.item.kind)
            {
                case FpItem::Knob:
                case FpItem::Stepped:
                {
                    g.setFont (hwFont (11.5f));
                    g.drawText (pl.item.label, a.withHeight (16.0f), juce::Justification::centred);
                    auto* sl = dynamic_cast<juce::Slider*> (pl.main);
                    const auto kb = sl->getBounds().toFloat();
                    const auto c = kb.getCentre();
                    const float R = kb.getWidth() * 0.5f;
                    const auto rot = sl->getRotaryParameters();
                    juce::StringArray labels = pl.item.kind == FpItem::Stepped ? pl.choices : pl.item.scale;
                    const bool numeric = labels.size() == 1 && labels[0] == "#";   // printed 0..10 scale
                    if (labels.isEmpty())   // print the parameter's real values
                        if (auto* rp = proc.apvts.getParameter (pl.item.param))
                        {
                            const int count = pl.item.size == 0 ? 5 : 7;
                            for (int k = 0; k < count; ++k) labels.add (shortLabel (rp->getText ((float) k / (float) (count - 1), 0)));
                        }
                    const int n = numeric ? 11 : labels.size();
                    g.setFont (hwFont (pl.item.size == 0 ? 9.0f : 10.0f, false));
                    for (int k = 0; k < n; ++k)
                    {
                        const float t = n > 1 ? (float) k / (float) (n - 1) : 0.5f;
                        const float ang = rot.startAngleRadians + t * (rot.endAngleRadians - rot.startAngleRadians);
                        auto p = [&] (float rad) { return juce::Point<float> (c.x + rad * std::sin (ang), c.y - rad * std::cos (ang)); };
                        g.drawLine ({ p (R + 3.0f), p (R + 8.0f) }, 1.2f);
                        const juce::String text = numeric ? ((pl.item.size == 0 && k % 2 == 1) ? juce::String() : juce::String (k)) : abbreviate (labels[k]);
                        if (text.isEmpty()) continue;
                        const auto tp = p (R + 17.0f);
                        g.drawText (text, juce::Rectangle<float> (tp.x - 22.0f, tp.y - 7.0f, 44.0f, 14.0f), juce::Justification::centred);
                    }
                    break;
                }
                case FpItem::Buttons:
                case FpItem::Push:
                case FpItem::Display:
                case FpItem::Leds:
                    g.setFont (hwFont (11.5f));
                    g.drawText (pl.item.label, a.withHeight (16.0f), juce::Justification::centred);
                    break;
                case FpItem::Switch:
                    g.setFont (hwFont (11.0f));
                    g.drawText (pl.item.alt, a.withHeight (16.0f), juce::Justification::centred);
                    g.drawText (pl.item.label, a.withTop (a.getBottom() - 18.0f), juce::Justification::centred);
                    break;
                case FpItem::Gap:
                    g.setColour (ink.withAlpha (0.25f));
                    g.drawVerticalLine ((int) a.getCentreX(), a.getY() - 20.0f, a.getBottom() + 20.0f);
                    if (pl.item.label.isNotEmpty())
                    {
                        g.setColour (ink.withAlpha (0.8f));
                        g.setFont (hwFont (10.0f));
                        juce::GlyphArrangement ga;
                        ga.addLineOfText (hwFont (10.0f), pl.item.label, 0.0f, 0.0f);
                        juce::Path path;
                        ga.createPath (path);
                        const auto pb = path.getBounds();
                        g.fillPath (path, juce::AffineTransform::rotation (-juce::MathConstants<float>::halfPi)
                                              .translated (a.getCentreX() - 5.0f, a.getCentreY() + pb.getWidth() * 0.5f));
                    }
                    break;
                case FpItem::Vu:
                case FpItem::Tap:
                    break;
            }
        }
    }

    void FaceplateView::paint (juce::Graphics& g)
    {
        if (bg.isValid()) g.drawImageAt (bg, 0, 0);
    }

    void FaceplateView::update (float grDb)
    {
        for (auto& pl : placed)
        {
            if (auto* vu = dynamic_cast<VuMeter*> (pl.main)) vu->setGr (grDb);
            else if (auto* led = dynamic_cast<LedLadder*> (pl.main)) led->setGr (grDb);
            else if (auto* lcd = dynamic_cast<LcdDisplay*> (pl.main)) lcd->refresh();
        }
    }

    Faceplate FaceplateView::fromSections (const Processor& p)
    {
        const auto info = p.getInfo();
        Faceplate f;
        f.title = info.name.fromFirstOccurrenceOf ("LiveTest ", false, false).toUpperCase();
        f.subtitle = "SQ model: " + info.sqName + "  -  " + info.modeledOn;
        f.panel = 0xff26292e; f.ink = 0xffdfe3e8; f.led = 0xff35d0ff;
        f.finish = Faceplate::Brushed;
        f.knobs = Faceplate::Silver;

        std::vector<FpItem> row;
        int rowW = 0;
        const int maxW = 1100;
        auto flush = [&] { if (! row.empty()) { f.rows.push_back (row); row.clear(); rowW = 0; } };
        for (const auto& s : p.getSections())
        {
            if (s.role == Section::Extra) continue;
            std::vector<FpItem> group;
            group.push_back ([&] { FpItem g; g.kind = FpItem::Gap; g.label = s.title; return g; }());
            if (s.onParam.isNotEmpty()) group.push_back (fpPush (s.onParam, "IN"));
            for (const auto& c : s.controls)
            {
                switch (c.type)
                {
                    case Ctl::Knob: group.push_back (fpKnob (c.paramId, c.label, 0)); break;
                    case Ctl::Toggle: group.push_back (fpPush (c.paramId, c.label)); break;
                    case Ctl::Choice:
                    case Ctl::Segment:
                    {
                        auto* cp = dynamic_cast<juce::AudioParameterChoice*> (p.apvts.getParameter (c.paramId));
                        const int n = cp != nullptr ? cp->choices.size() : 0;
                        group.push_back (n <= 4 ? fpButtons (c.paramId, c.label) : fpStepped (c.paramId, c.label, 0));
                        break;
                    }
                    case Ctl::Tap: group.push_back (fpTap()); break;
                }
            }
            int gw = 0;
            for (auto& it : group)
            {
                juce::StringArray ch;
                if (auto* cp = dynamic_cast<juce::AudioParameterChoice*> (p.apvts.getParameter (it.param))) ch = cp->choices;
                gw += naturalSize (it, ch).x + itemGap;
            }
            if (rowW > 0 && rowW + gw > maxW) flush();
            for (auto& it : group) row.push_back (it);
            rowW += gw;
        }
        flush();
        return f;
    }
}
