// SPDX-License-Identifier: AGPL-3.0-only
// Copyright (C) 2026 Jerod Ulicki

// LiveTest Updater - installs, updates and removes the LiveTest plugins.
// It reads manifest.json from the repo's "latest-build" release, compares each plugin's
// version with what is installed in the system VST3 folder, downloads the zips that are
// needed and installs them in one go (one password / UAC prompt per batch). Old copies,
// including duplicates in the user's own VST3 folder on macOS, are removed first.

#include <juce_gui_basics/juce_gui_basics.h>
#include "Theme.h"
#include "Elevate.h"

namespace
{
    const juce::String repo = LTP_REPO;
    juce::String releaseBase() { return "https://github.com/" + repo + "/releases/download/latest-build/"; }
    juce::String releasePage() { return "https://github.com/" + repo + "/releases/tag/latest-build"; }

    juce::String platformKey()
    {
       #if JUCE_MAC
        return "mac";
       #elif JUCE_WINDOWS
        return "windows";
       #else
        return "linux";
       #endif
    }

    int compareVersions (const juce::String& a, const juce::String& b)
    {
        auto ta = juce::StringArray::fromTokens (a, ".", ""), tb = juce::StringArray::fromTokens (b, ".", "");
        for (int i = 0; i < std::max (ta.size(), tb.size()); ++i)
        {
            const int x = ta[i].getIntValue(), y = tb[i].getIntValue();
            if (x != y) return x < y ? -1 : 1;
        }
        return 0;
    }

    juce::File vst3Folder()
    {
       #if JUCE_MAC
        return juce::File ("/Library/Audio/Plug-Ins/VST3");
       #elif JUCE_WINDOWS
        return juce::File (juce::SystemStats::getEnvironmentVariable ("CommonProgramFiles", "C:\\Program Files\\Common Files")).getChildFile ("VST3");
       #else
        return juce::File::getSpecialLocation (juce::File::userHomeDirectory).getChildFile (".vst3");
       #endif
    }

    // a per-user copy that would shadow / duplicate the system one (macOS only)
    juce::File userVst3Folder()
    {
       #if JUCE_MAC
        return juce::File::getSpecialLocation (juce::File::userHomeDirectory).getChildFile ("Library/Audio/Plug-Ins/VST3");
       #else
        return {};
       #endif
    }

    juce::File appDataDir()
    {
        auto base = juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory);
       #if JUCE_MAC
        base = base.getChildFile ("Application Support");
       #endif
        return base.getChildFile ("LiveTestPlugin");
    }

    juce::File workDir() { return juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("LiveTestUpdater"); }

    // ------------------------------------------------------------ installed-version record
    juce::var loadRecord()
    {
        auto v = juce::JSON::parse (appDataDir().getChildFile ("installed.json"));
        return v.isObject() ? v : juce::var (new juce::DynamicObject());
    }

    void saveRecord (const juce::var& record)
    {
        appDataDir().createDirectory();
        appDataDir().getChildFile ("installed.json").replaceWithText (juce::JSON::toString (record));
    }

    // version written into the bundle at build time
    juce::String bundleVersion (const juce::File& bundle)
    {
        const auto plist = bundle.getChildFile ("Contents/Info.plist");
        if (plist.existsAsFile())
            if (auto xml = juce::XmlDocument::parse (plist))
                if (auto* dict = xml->getChildByName ("dict"))
                {
                    bool next = false;
                    for (auto* e : dict->getChildIterator())
                    {
                        if (next) return e->getAllSubText().trim();
                        next = e->hasTagName ("key") && e->getAllSubText().trim() == "CFBundleShortVersionString";
                    }
                }
        // moduleinfo.json is JSON5 (trailing commas), so read the first "Version" by hand
        const auto text = bundle.getChildFile ("Contents/Resources/moduleinfo.json").loadFileAsString();
        const auto after = text.fromFirstOccurrenceOf ("\"Version\"", false, false).fromFirstOccurrenceOf (":", false, false);
        const auto v = after.fromFirstOccurrenceOf ("\"", false, false).upToFirstOccurrenceOf ("\"", false, false).trim();
        return v.containsOnly ("0123456789.") ? v : juce::String();
    }

   #if JUCE_WINDOWS
    juce::String psQuote (const juce::String& s) { return "'" + s.replace ("'", "''") + "'"; }
   #else
    juce::String shQuote (const juce::String& s) { return "'" + s.replace ("'", "'\\''") + "'"; }
   #endif

    // ------------------------------------------------------------ network
    bool fetch (const juce::String& url, juce::MemoryBlock* toMemory, const juce::File* toFile,
                std::function<void (double)> progress, juce::String& error)
    {
        int status = 0;
        auto opts = juce::URL::InputStreamOptions (juce::URL::ParameterHandling::inAddress)
                        .withConnectionTimeoutMs (20000)
                        .withNumRedirectsToFollow (10)
                        .withStatusCode (&status);
        std::unique_ptr<juce::InputStream> in (juce::URL (url).createInputStream (opts));
        if (in == nullptr) { error = "Could not reach GitHub - check the internet connection."; return false; }
        if (status >= 400) { error = "GitHub answered " + juce::String (status) + " for " + url.fromLastOccurrenceOf ("/", false, false); return false; }

        const auto total = in->getTotalLength();
        std::unique_ptr<juce::FileOutputStream> out;
        if (toFile != nullptr)
        {
            toFile->deleteFile();
            out = std::make_unique<juce::FileOutputStream> (*toFile);
            if (out->failedToOpen()) { error = "Could not write " + toFile->getFullPathName(); return false; }
        }
        juce::HeapBlock<char> buf (65536);
        juce::int64 done = 0;
        for (;;)
        {
            const int n = in->read (buf.get(), 65536);
            if (n <= 0) break;
            if (out) out->write (buf.get(), (size_t) n);
            if (toMemory) toMemory->append (buf.get(), (size_t) n);
            done += n;
            if (progress && total > 0) progress ((double) done / (double) total);
            if (juce::Thread::currentThreadShouldExit()) { error = "Cancelled"; return false; }
        }
        if (out) out->flush();
        if (done == 0) { error = "Empty download from " + url; return false; }
        return true;
    }

    bool unzip (const juce::File& zip, const juce::File& dest, juce::String& error)
    {
        dest.deleteRecursively();
        dest.createDirectory();
       #if JUCE_MAC
        juce::ChildProcess cp;   // ditto keeps permissions and the code signature intact
        if (! cp.start (juce::StringArray { "/usr/bin/ditto", "-x", "-k", zip.getFullPathName(), dest.getFullPathName() }))
        { error = "Could not run ditto"; return false; }
        cp.waitForProcessToFinish (120000);
        if (cp.getExitCode() != 0) { error = "Could not unzip " + zip.getFileName(); return false; }
        return true;
       #else
        juce::ZipFile zf (zip);
        if (zf.getNumEntries() == 0) { error = zip.getFileName() + " is not a valid zip"; return false; }
        const auto r = zf.uncompressTo (dest);
        if (r.failed()) { error = r.getErrorMessage(); return false; }
        return true;
       #endif
    }

    bool runElevated (const juce::String& script, juce::String& error)
    {
        workDir().createDirectory();
        std::string err;
       #if JUCE_WINDOWS
        auto f = workDir().getChildFile ("install.ps1");
        f.replaceWithText (script, true, true);   // UTF-16 with BOM, so any path survives
        const int code = ltpRunElevatedScript (f.getFullPathName().toWideCharPointer(), err);
       #else
        auto f = workDir().getChildFile ("install.sh");
        f.replaceWithText (script, false, false, "\n");   // Unix line endings - JUCE defaults to \r\n, which breaks sh
        const int code = ltpRunElevatedScript (f.getFullPathName().toStdString(), err);
       #endif
        if (code != 0) error = err.empty() ? juce::String ("Install script failed") : juce::String (err);
        return code == 0;
    }

    // ------------------------------------------------------------ model
    struct Entry
    {
        enum Job { None, Queued, Working, Done, Failed };
        juce::String id, name, family, sq, modeledOn, latest, installed, asset;
        bool selected = false;
        // progress of the current / last install or remove for this plugin
        Job job = None;
        juce::String jobText;
        double jobProgress = 0.0;
    };

    class Worker : public juce::Thread
    {
    public:
        explicit Worker (std::function<void (Worker&)> f) : juce::Thread ("LiveTest Updater"), fn (std::move (f)) {}
        ~Worker() override { stopThread (10000); }
        void run() override { fn (*this); }
    private:
        std::function<void (Worker&)> fn;
    };
}

// ================================================================ main component
class UpdaterComponent : public juce::Component, private juce::TableListBoxModel
{
public:
    explicit UpdaterComponent (bool demo = false)
    {
        theme::setLightMode (true);   // the updater always uses the light look (not saved, so plugins keep their own setting)
        lnf.applyColours();
        setLookAndFeel (&lnf);

        for (auto* b : { &refreshBtn, &allBtn, &updatesBtn, &noneBtn, &installBtn, &removeBtn, &folderBtn, &updaterBtn })
            addAndMakeVisible (b);
        installBtn.setColour (juce::TextButton::buttonColourId, theme::eq);
        installBtn.setColour (juce::TextButton::textColourOffId, theme::onAccent);
        updaterBtn.setVisible (false);

        refreshBtn.onClick = [this] { refresh(); };
        allBtn.onClick = [this] { for (auto& e : entries) e.selected = true; table.repaint(); updateButtons(); };
        noneBtn.onClick = [this] { for (auto& e : entries) e.selected = false; table.repaint(); updateButtons(); };
        updatesBtn.onClick = [this] { for (auto& e : entries) e.selected = needsInstall (e); table.repaint(); updateButtons(); };
        installBtn.onClick = [this] { install(); };
        removeBtn.onClick = [this] { remove(); };
        folderBtn.onClick = [] { auto f = vst3Folder(); if (f.isDirectory()) f.revealToUser(); };
        updaterBtn.onClick = [this] { juce::URL (updaterUrl.isNotEmpty() ? updaterUrl : releasePage()).launchInDefaultBrowser(); };

        table.setModel (this);
        table.setRowHeight (30);
        table.setHeaderHeight (28);
        auto& h = table.getHeader();
        const int colFlags = juce::TableHeaderComponent::visible;
        h.addColumn ("", 1, 36, 36, 36, colFlags);
        h.addColumn ("Plugin", 2, 190, 120, 400, colFlags | juce::TableHeaderComponent::resizable);
        h.addColumn ("Type", 3, 84, 60, 200, colFlags | juce::TableHeaderComponent::resizable);
        h.addColumn ("SQ model", 4, 150, 80, 300, colFlags | juce::TableHeaderComponent::resizable);
        h.addColumn ("Installed", 5, 84, 60, 200, colFlags | juce::TableHeaderComponent::resizable);
        h.addColumn ("Latest", 6, 84, 60, 200, colFlags | juce::TableHeaderComponent::resizable);
        h.addColumn ("Status", 7, 150, 80, 400, colFlags | juce::TableHeaderComponent::resizable);
        h.addColumn ("Progress", 8, 210, 120, 400, colFlags | juce::TableHeaderComponent::resizable);
        h.setStretchToFitActive (true);
        addAndMakeVisible (table);

        status.setFont (theme::font (12.5f));
        status.setColour (juce::Label::textColourId, theme::textDim);
        addAndMakeVisible (status);
        progressBar.setColour (juce::ProgressBar::foregroundColourId, theme::eq);
        progressBar.setColour (juce::ProgressBar::backgroundColourId, theme::panelHi);
        addAndMakeVisible (progressBar);

        setSize (1020, 680);
        styleTable();
        if (demo) loadDemo(); else refresh();
    }

    void loadDemo()
    {
        const char* rows[][5] = { { "OptoComp", "LiveTest Opto Comp", "Dynamics", "Opto", "0.1.0" }, { "Fet76", "LiveTest FET 76", "Dynamics", "Peak Limiter 76", "0.1.0" },
                                  { "DynMulti", "LiveTest Dyn8", "Dynamics", "Dyn8", "0.1.0" }, { "Reverb", "LiveTest Reverb", "FX", "SMR Reverb", "0.1.1" },
                                  { "TapDelay", "LiveTest Tap Delay", "FX", "Stereo Tap Delay", "0.1.0" }, { "TapeEcho", "LiveTest Tape Echo", "FX", "Echo", "0.1.0" } };
        const char* inst[] = { "0.1.0", "", "0.1.0", "0.1.0", "", "?" };
        int i = 0;
        for (auto& r : rows)
        {
            Entry e; e.id = r[0]; e.name = r[1]; e.family = r[2]; e.sq = r[3]; e.latest = r[4]; e.asset = "x.zip"; e.installed = inst[i];
            e.selected = i == 1 || i == 3;
            entries.push_back (e); ++i;
        }
        table.updateContent();
        updateButtons();
        entries[1].job = Entry::Working; entries[1].jobText = "Downloading"; entries[1].jobProgress = 0.46;
        entries[3].job = Entry::Done; entries[3].jobText = "Installed v0.1.1"; entries[3].jobProgress = 1.0;
        updateOverall();
        setStatus ("Release 0.1.0 (2026-11-15 19:20 UTC): 21 plugins, 4 installed, 3 need installing or updating.");
        updaterBtn.setButtonText ("New updater v0.2.0 - download");
        updaterBtn.setColour (juce::TextButton::buttonColourId, theme::warn);
        updaterBtn.setColour (juce::TextButton::textColourOffId, theme::onAccent);
        updaterBtn.setVisible (true);
        resized();
    }

    ~UpdaterComponent() override
    {
        worker.reset();
        setLookAndFeel (nullptr);
    }

    void paint (juce::Graphics& g) override
    {
        g.fillAll (theme::bg);
        g.setColour (theme::text);
        g.setFont (theme::font (17.0f, true));
        g.drawText ("LIVETEST", 14, 8, 100, 24, juce::Justification::centredLeft);
        g.setColour (theme::eq);
        g.drawText ("UPDATER", 104, 8, 200, 24, juce::Justification::centredLeft);
        g.setColour (theme::textFaint);
        g.setFont (theme::font (10.5f));
        g.drawText ("v" LTP_UPDATER_VERSION "  -  installs into " + vst3Folder().getFullPathName() + " (all users)",
                    15, 30, getWidth() - 300, 14, juce::Justification::centredLeft);
        g.setColour (theme::border);
        g.drawHorizontalLine (headerH - 1, 0.0f, (float) getWidth());

        g.setColour (theme::textDim);
        g.setFont (theme::font (12.0f));
       #if JUCE_WINDOWS
        const juce::String ask = "administrator permission";
       #else
        const juce::String ask = "password";
       #endif
        g.drawText ("Close LiveProfessor (or any host) before installing, then rescan plug-ins. "
                    "You'll be asked for your " + ask + " once per install.",
                    12, headerH + 6, getWidth() - 24, 18, juce::Justification::centredLeft);
    }

    void resized() override
    {
        auto r = getLocalBounds();
        auto head = r.removeFromTop (headerH).reduced (10, 0);
        const int cy = headerH / 2;
        auto right = [&] (juce::Component& c, int w) { c.setBounds (head.removeFromRight (w).withHeight (26).withY (cy - 13)); head.removeFromRight (8); };
        right (refreshBtn, 90);
        right (updaterBtn, 240);

        r.removeFromTop (30);
        r.reduce (10, 0);
        auto bottom = r.removeFromBottom (92);
        table.setBounds (r);

        bottom.removeFromTop (10);
        auto row = bottom.removeFromTop (30);
        allBtn.setBounds (row.removeFromLeft (60)); row.removeFromLeft (4);
        updatesBtn.setBounds (row.removeFromLeft (90)); row.removeFromLeft (4);
        noneBtn.setBounds (row.removeFromLeft (60));
        installBtn.setBounds (row.removeFromRight (220)); row.removeFromRight (8);
        removeBtn.setBounds (row.removeFromRight (150)); row.removeFromRight (8);
        folderBtn.setBounds (row.removeFromRight (110));
        bottom.removeFromTop (10);
        auto line = bottom.removeFromTop (26);
        progressBar.setBounds (line.removeFromRight (360));
        line.removeFromRight (10);
        status.setBounds (line);
    }

private:
    static constexpr int headerH = 50;

    // ------------------------------------------------------------ table model
    int getNumRows() override { return (int) entries.size(); }

    void paintRowBackground (juce::Graphics& g, int row, int w, int h, bool) override
    {
        if (row >= (int) entries.size()) return;
        g.setColour (entries[(size_t) row].selected ? theme::eq.withAlpha (0.14f) : (row % 2 ? theme::panel : theme::panelHi));
        g.fillRect (0, 0, w, h);
    }

    void paintCell (juce::Graphics& g, int row, int col, int w, int h, bool) override
    {
        if (row >= (int) entries.size()) return;
        const auto& e = entries[(size_t) row];
        if (col == 1)
        {
            auto box = juce::Rectangle<float> (0, 0, 16, 16).withCentre ({ (float) w * 0.5f, (float) h * 0.5f });
            g.setColour (e.selected ? theme::eq : theme::border);
            if (e.selected) g.fillRoundedRectangle (box, 3.0f); else g.drawRoundedRectangle (box, 3.0f, 1.5f);
            if (e.selected)
            {
                juce::Path tick;
                tick.startNewSubPath (box.getX() + 3.5f, box.getCentreY());
                tick.lineTo (box.getX() + 7.0f, box.getBottom() - 4.0f);
                tick.lineTo (box.getRight() - 3.0f, box.getY() + 4.0f);
                g.setColour (theme::onAccent);
                g.strokePath (tick, juce::PathStrokeType (2.0f));
            }
            return;
        }
        if (col == 8)
        {
            if (e.job == Entry::None) return;
            auto bar = juce::Rectangle<float> (6.0f, (float) h * 0.5f - 10.0f, (float) w - 12.0f, 20.0f);
            g.setColour (theme::bg);
            g.fillRoundedRectangle (bar, 4.0f);
            const auto fill = e.job == Entry::Failed ? theme::danger : e.job == Entry::Done ? theme::good : theme::eq;
            const float frac = (float) juce::jlimit (0.0, 1.0, e.jobProgress);
            if (frac > 0.0f)
            {
                g.setColour (fill.withAlpha (e.job == Entry::Working ? 0.75f : 0.9f));
                g.fillRoundedRectangle (bar.withWidth (std::max (6.0f, bar.getWidth() * frac)), 4.0f);
            }
            g.setColour (theme::border);
            g.drawRoundedRectangle (bar, 4.0f, 1.0f);
            juce::String t = e.jobText;
            if (e.job == Entry::Working) t << "  " << juce::String (juce::roundToInt (frac * 100.0f)) << "%";
            g.setColour (theme::text);
            g.setFont (theme::font (12.0f, true));
            g.drawText (t, bar.reduced (6.0f, 0.0f).toNearestInt(), juce::Justification::centred, true);
            return;
        }
        juce::String text;
        juce::Colour c = theme::text;
        bool bold = false;
        switch (col)
        {
            case 2: text = e.name.fromFirstOccurrenceOf ("LiveTest ", false, false); bold = true; break;
            case 3: text = e.family; c = e.family == "FX" ? theme::eq : theme::comp; break;
            case 4: text = e.sq; c = theme::textDim; break;
            case 5: text = e.installed.isEmpty() ? juce::String ("-") : e.installed; c = theme::textDim; break;
            case 6: text = e.latest; c = theme::textDim; break;
            case 7: text = statusText (e); c = statusColour (e); bold = true; break;
            default: break;
        }
        g.setColour (c);
        g.setFont (theme::font (13.0f, bold));
        g.drawText (text, 6, 0, w - 8, h, juce::Justification::centredLeft, true);
    }

    void cellClicked (int row, int, const juce::MouseEvent&) override
    {
        if (row < 0 || row >= (int) entries.size() || busy) return;
        entries[(size_t) row].selected = ! entries[(size_t) row].selected;
        table.repaintRow (row);
        updateButtons();
    }

    juce::String getCellTooltip (int row, int) override
    {
        if (row < 0 || row >= (int) entries.size()) return {};
        const auto& e = entries[(size_t) row];
        return e.name + " - SQ model: " + e.sq + "\nModeled on: " + e.modeledOn;
    }

    static bool needsInstall (const Entry& e) { return e.latest.isNotEmpty() && (e.installed.isEmpty() || compareVersions (e.installed, e.latest) < 0); }

    static juce::String statusText (const Entry& e)
    {
        // while a job runs (and right after it) the status follows it live
        if (e.job == Entry::Queued) return "Queued";
        if (e.job == Entry::Working) return e.jobText + "...";
        if (e.job == Entry::Failed) return e.jobText;
        if (e.latest.isEmpty()) return "Not in this release";
        if (e.installed.isEmpty()) return "Not installed";
        if (e.installed == "?") return "Installed (version unknown)";
        const int c = compareVersions (e.installed, e.latest);
        return c < 0 ? "Update available" : c == 0 ? "Up to date" : "Newer than release";
    }

    static juce::Colour statusColour (const Entry& e)
    {
        if (e.job == Entry::Queued) return theme::textDim;
        if (e.job == Entry::Working) return theme::eq;
        if (e.job == Entry::Failed) return theme::danger;
        if (e.installed.isEmpty()) return theme::textDim;
        if (e.installed == "?" || needsInstall (e)) return theme::warn;
        return theme::good;
    }

    // ------------------------------------------------------------ progress
    Entry* findEntry (const juce::String& id)
    {
        for (auto& e : entries) if (e.id == id) return &e;
        return nullptr;
    }

    void setItem (const juce::String& id, Entry::Job job, const juce::String& text, double p)
    {
        if (auto* e = findEntry (id))
        {
            e->job = job; e->jobText = text; e->jobProgress = p;
            // show the result straight away in the Installed / Status columns
            if (job == Entry::Done && text.startsWith ("Installed")) e->installed = e->latest;
            if (job == Entry::Done && text == "Removed") e->installed = {};
            table.repaintRow ((int) (e - entries.data()));
        }
        updateOverall();
    }

    // overall = average of every plugin in the current job; text "Overall: 3 of 5 done - 62%"
    void updateOverall()
    {
        int n = 0, done = 0, failed = 0;
        double sum = 0.0;
        for (auto& e : entries)
        {
            if (e.job == Entry::None) continue;
            ++n;
            sum += juce::jlimit (0.0, 1.0, e.jobProgress);
            if (e.job == Entry::Done) ++done;
            if (e.job == Entry::Failed) ++failed;
        }
        if (n == 0) { progress = 0.0; progressBar.setTextToDisplay ("Overall progress"); return; }
        progress = sum / n;
        juce::String t = "Overall: " + juce::String (done) + " of " + juce::String (n) + " done";
        if (failed > 0) t << ", " << failed << " failed";
        t << "  -  " << juce::roundToInt (progress * 100.0) << "%";
        progressBar.setTextToDisplay (t);
    }

    static void postItem (juce::Component::SafePointer<UpdaterComponent> safe, juce::String id, Entry::Job job, juce::String text, double p)
    {
        juce::MessageManager::callAsync ([safe, id, job, text, p] { if (safe) safe->setItem (id, job, text, p); });
    }

    // ------------------------------------------------------------ actions
    void setStatus (const juce::String& s, bool isError = false)
    {
        status.setText (s, juce::dontSendNotification);
        status.setColour (juce::Label::textColourId, isError ? theme::danger : theme::textDim);
    }

    void setBusy (bool b)
    {
        busy = b;
        updateButtons();
        refreshBtn.setEnabled (! b);
    }

    void updateButtons()
    {
        int sel = 0, installedSel = 0, updates = 0;
        for (auto& e : entries)
        {
            if (e.selected) { ++sel; if (e.installed.isNotEmpty()) ++installedSel; }
            if (needsInstall (e)) ++updates;
        }
        installBtn.setButtonText (sel > 0 ? "INSTALL / UPDATE (" + juce::String (sel) + ")" : "INSTALL / UPDATE");
        installBtn.setEnabled (! busy && sel > 0);
        removeBtn.setEnabled (! busy && installedSel > 0);
        updatesBtn.setButtonText ("Updates (" + juce::String (updates) + ")");
        for (auto* b : { &allBtn, &updatesBtn, &noneBtn }) b->setEnabled (! busy);
    }

    void scanInstalled()
    {
        const auto record = loadRecord();
        for (auto& e : entries)
        {
            const auto bundle = vst3Folder().getChildFile (e.name + ".vst3");
            if (! bundle.exists()) { e.installed = {}; continue; }
            auto v = bundleVersion (bundle);
            if (v.isEmpty()) v = record[juce::Identifier (e.id)].toString();
            e.installed = v.isEmpty() ? juce::String ("?") : v;
        }
    }

    void refresh()
    {
        if (busy) return;
        setBusy (true);
        progress = -1.0;   // indeterminate while checking
        progressBar.setTextToDisplay ({});
        setStatus ("Checking GitHub for the latest LiveTest release...");
        juce::Component::SafePointer<UpdaterComponent> safe (this);
        startWorker ([safe] (Worker&)
        {
            juce::MemoryBlock mb;
            juce::String err;
            const bool ok = fetch (releaseBase() + "manifest.json", &mb, nullptr, nullptr, err);
            const auto json = ok ? juce::JSON::parse (mb.toString()) : juce::var();
            juce::MessageManager::callAsync ([safe, ok, json, err]
            {
                if (safe == nullptr) return;
                safe->applyManifest (ok, json, err);
            });
        });
    }

    void applyManifest (bool ok, const juce::var& json, const juce::String& err)
    {
        progress = 0.0;
        progressBar.setTextToDisplay ("Overall progress");
        if (! ok || ! json.isObject())
        {
            setBusy (false);
            setStatus (ok ? "The release manifest could not be read." : err, true);
            return;
        }
        std::map<juce::String, bool> wasSelected;
        for (auto& e : entries) wasSelected[e.id] = e.selected;
        entries.clear();
        if (auto* arr = json["plugins"].getArray())
            for (auto& p : *arr)
            {
                Entry e;
                e.id = p["id"].toString(); e.name = p["name"].toString(); e.family = p["family"].toString();
                e.sq = p["sq"].toString(); e.modeledOn = p["modeledOn"].toString(); e.latest = p["version"].toString();
                e.asset = p["assets"][juce::Identifier (platformKey())].toString();
                e.selected = wasSelected.count (e.id) ? wasSelected[e.id] : false;
                if (e.asset.isEmpty()) e.latest = {};
                entries.push_back (e);
            }
        scanInstalled();

        const auto upd = json["updater"];
        const auto newest = upd["version"].toString();
        updaterUrl = upd["assets"][juce::Identifier (platformKey())].toString();
        if (updaterUrl.isNotEmpty()) updaterUrl = releaseBase() + updaterUrl;
        const bool newer = newest.isNotEmpty() && compareVersions (LTP_UPDATER_VERSION, newest) < 0;
        updaterBtn.setButtonText ("New updater v" + newest + " - download");
        updaterBtn.setColour (juce::TextButton::buttonColourId, theme::warn);
        updaterBtn.setColour (juce::TextButton::textColourOffId, theme::onAccent);
        updaterBtn.setVisible (newer);

        table.updateContent();
        table.repaint();
        int updates = 0, installed = 0;
        for (auto& e : entries) { if (needsInstall (e)) ++updates; if (e.installed.isNotEmpty()) ++installed; }
        setBusy (false);
        setStatus ("Release " + json["suiteVersion"].toString() + " (" + json["built"].toString() + "): "
                   + juce::String ((int) entries.size()) + " plugins, " + juce::String (installed) + " installed, "
                   + juce::String (updates) + (updates == 1 ? " needs" : " need") + " installing or updating.");
    }

    void install()
    {
        std::vector<Entry> todo;
        for (auto& e : entries) if (e.selected && e.asset.isNotEmpty()) todo.push_back (e);
        if (todo.empty() || busy) return;
        setBusy (true);
        for (auto& e : entries) { e.job = Entry::None; e.jobText = {}; e.jobProgress = 0.0; }
        for (auto& e : todo) if (auto* x = findEntry (e.id)) { x->job = Entry::Queued; x->jobText = "Queued"; }
        table.repaint();
        updateOverall();
        juce::Component::SafePointer<UpdaterComponent> safe (this);
        startWorker ([safe, todo] (Worker& w)
        {
            auto post = [safe] (juce::String text, bool err) { juce::MessageManager::callAsync ([safe, text, err] { if (safe) safe->setStatus (text, err); }); };
            const auto dir = workDir();
            dir.deleteRecursively();
            dir.createDirectory();

            juce::StringArray bundles;
            juce::String err;
            for (size_t i = 0; i < todo.size(); ++i)
            {
                const auto& e = todo[i];
                post ("Downloading " + e.name + " (" + juce::String ((int) i + 1) + " of " + juce::String ((int) todo.size()) + ")...", false);
                const auto zip = dir.getChildFile (e.id + ".zip");
                postItem (safe, e.id, Entry::Working, "Downloading", 0.0);
                auto lastSent = std::make_shared<double> (-1.0);
                auto failAll = [&] (const juce::String& why)
                {
                    postItem (safe, e.id, Entry::Failed, "Failed", 1.0);
                    for (size_t k = i + 1; k < todo.size(); ++k) postItem (safe, todo[k].id, Entry::Failed, "Not installed", 0.0);
                    post (e.name + ": " + why, true);
                    finish (safe, false);
                };
                if (! fetch (releaseBase() + e.asset, nullptr, &zip, [safe, id = e.id, lastSent] (double p)
                             {
                                 if (p - *lastSent < 0.01 && p < 1.0) return;   // about 100 updates per download
                                 *lastSent = p;
                                 postItem (safe, id, Entry::Working, "Downloading", p * 0.85);
                             }, err))
                    return failAll (err);
                postItem (safe, e.id, Entry::Working, "Unpacking", 0.88);
                if (! unzip (zip, dir.getChildFile (e.id), err)) return failAll (err);
                auto found = dir.getChildFile (e.id).findChildFiles (juce::File::findDirectories, false, "*.vst3");
                if (found.isEmpty()) return failAll ("the download didn't contain a .vst3");
                bundles.add (found[0].getFullPathName());
                postItem (safe, e.id, Entry::Working, "Ready to install", 0.92);
                if (w.threadShouldExit()) return;
            }

            post ("Installing " + juce::String ((int) todo.size()) + " plugin(s) - approve the permission prompt...", false);
            for (auto& e : todo) postItem (safe, e.id, Entry::Working, "Installing", 0.95);
            if (! runElevated (installScript (todo, bundles), err))
            {
                for (auto& e : todo) postItem (safe, e.id, Entry::Failed, "Not installed", 0.92);
                post (err, true);
                return finish (safe, false);
            }
            for (auto& e : todo) postItem (safe, e.id, Entry::Done, "Installed v" + e.latest, 1.0);
            auto record = loadRecord();
            for (auto& e : todo) record.getDynamicObject()->setProperty (juce::Identifier (e.id), e.latest);
            saveRecord (record);
            dir.deleteRecursively();
            post ("Installed " + juce::String ((int) todo.size()) + " plugin(s). Reopen your host and rescan plug-ins.", false);
            finish (safe, true);
        });
    }

    void remove()
    {
        std::vector<Entry> todo;
        for (auto& e : entries) if (e.selected && e.installed.isNotEmpty()) todo.push_back (e);
        if (todo.empty() || busy) return;
        const auto msg = "Remove " + juce::String ((int) todo.size()) + " LiveTest plugin(s) from " + vst3Folder().getFullPathName() + "?";
        juce::Component::SafePointer<UpdaterComponent> safe (this);
        juce::AlertWindow::showOkCancelBox (juce::MessageBoxIconType::QuestionIcon, "Remove plugins", msg, "Remove", "Cancel", this,
            juce::ModalCallbackFunction::create ([safe, todo] (int result)
            {
                if (result == 0 || safe == nullptr) return;
                safe->setBusy (true);
                safe->setStatus ("Removing - approve the permission prompt...");
                for (auto& e : safe->entries) { e.job = Entry::None; e.jobText = {}; e.jobProgress = 0.0; }
                for (auto& e : todo) if (auto* x = safe->findEntry (e.id)) { x->job = Entry::Working; x->jobText = "Removing"; x->jobProgress = 0.5; }
                safe->table.repaint();
                safe->updateOverall();
                safe->startWorker ([safe, todo] (Worker&)
                {
                    juce::String err;
                    const bool ok = runElevated (removeScript (todo), err);
                    for (auto& e : todo) postItem (safe, e.id, ok ? Entry::Done : Entry::Failed, ok ? "Removed" : "Not removed", 1.0);
                    if (ok)
                    {
                        auto record = loadRecord();
                        for (auto& e : todo) record.getDynamicObject()->removeProperty (juce::Identifier (e.id));
                        saveRecord (record);
                    }
                    juce::MessageManager::callAsync ([safe, ok, err, n = (int) todo.size()]
                    {
                        if (safe == nullptr) return;
                        safe->setStatus (ok ? "Removed " + juce::String (n) + " plugin(s)." : err, ! ok);
                    });
                    finish (safe, ok);
                });
            }));
    }

    static void finish (juce::Component::SafePointer<UpdaterComponent> safe, bool ok)
    {
        juce::MessageManager::callAsync ([safe, ok]
        {
            if (safe == nullptr) return;
            safe->scanInstalled();
            safe->updateOverall();
            if (ok) for (auto& e : safe->entries) e.selected = false;
            safe->table.updateContent();
            safe->table.repaint();
            safe->setBusy (false);
        });
    }

    // ------------------------------------------------------------ scripts
    static juce::String installScript (const std::vector<Entry>& todo, const juce::StringArray& bundles)
    {
        const auto dest = vst3Folder().getFullPathName();
       #if JUCE_WINDOWS
        juce::String s = "$ErrorActionPreference = 'Stop'\r\ntry {\r\n";
        s << "  $dest = " << psQuote (dest) << "\r\n  New-Item -ItemType Directory -Force -Path $dest | Out-Null\r\n";
        for (size_t i = 0; i < todo.size(); ++i)
        {
            s << "  $t = Join-Path $dest " << psQuote (todo[i].name + ".vst3") << "\r\n";
            s << "  if (Test-Path -LiteralPath $t) { Remove-Item -LiteralPath $t -Recurse -Force }\r\n";
            s << "  Copy-Item -LiteralPath " << psQuote (bundles[(int) i]) << " -Destination $t -Recurse -Force\r\n";
        }
        s << "  exit 0\r\n} catch {\r\n  $_ | Out-File -FilePath " << psQuote (workDir().getChildFile ("error.txt").getFullPathName()) << "\r\n  exit 1\r\n}\r\n";
        return s;
       #else
        juce::String s = "#!/bin/sh\nset -e\nDEST=" + shQuote (dest) + "\nmkdir -p \"$DEST\"\n";
        const auto user = userVst3Folder();
        for (size_t i = 0; i < todo.size(); ++i)
        {
            const auto bundle = todo[i].name + ".vst3";
            s << "rm -rf \"$DEST\"/" << shQuote (bundle) << "\n";
            if (user != juce::File()) s << "rm -rf " << shQuote (user.getChildFile (bundle).getFullPathName()) << "\n";
            s << "/usr/bin/ditto " << shQuote (bundles[(int) i]) << " \"$DEST\"/" << shQuote (bundle) << "\n";
            s << "/usr/bin/xattr -dr com.apple.quarantine \"$DEST\"/" << shQuote (bundle) << " 2>/dev/null || true\n";
            s << "chmod -R a+rX \"$DEST\"/" << shQuote (bundle) << "\n";
        }
        return s;
       #endif
    }

    static juce::String removeScript (const std::vector<Entry>& todo)
    {
        const auto dest = vst3Folder().getFullPathName();
       #if JUCE_WINDOWS
        juce::String s = "$ErrorActionPreference = 'Stop'\r\ntry {\r\n";
        for (auto& e : todo)
            s << "  $t = Join-Path " << psQuote (dest) << " " << psQuote (e.name + ".vst3") << "\r\n"
              << "  if (Test-Path -LiteralPath $t) { Remove-Item -LiteralPath $t -Recurse -Force }\r\n";
        s << "  exit 0\r\n} catch { exit 1 }\r\n";
        return s;
       #else
        juce::String s = "#!/bin/sh\nset -e\n";
        for (auto& e : todo)
        {
            s << "rm -rf " << shQuote (juce::File (dest).getChildFile (e.name + ".vst3").getFullPathName()) << "\n";
            if (userVst3Folder() != juce::File()) s << "rm -rf " << shQuote (userVst3Folder().getChildFile (e.name + ".vst3").getFullPathName()) << "\n";
        }
        return s;
       #endif
    }

    void startWorker (std::function<void (Worker&)> fn)
    {
        worker.reset();
        worker = std::make_unique<Worker> (std::move (fn));
        worker->startThread();
    }

    void styleTable()
    {
        table.setColour (juce::ListBox::backgroundColourId, theme::panel);
        table.setColour (juce::ListBox::outlineColourId, theme::border);
        table.setOutlineThickness (1);
        auto& h = table.getHeader();
        h.setColour (juce::TableHeaderComponent::backgroundColourId, theme::panelHi);
        h.setColour (juce::TableHeaderComponent::textColourId, theme::textDim);
        h.setColour (juce::TableHeaderComponent::outlineColourId, theme::border);
        installBtn.setColour (juce::TextButton::buttonColourId, theme::eq);
        installBtn.setColour (juce::TextButton::textColourOffId, theme::onAccent);
        progressBar.setColour (juce::ProgressBar::foregroundColourId, theme::eq);
        progressBar.setColour (juce::ProgressBar::backgroundColourId, theme::panelHi);
    }

    theme::LookAndFeel lnf;
    juce::TooltipWindow tooltips { this, 500 };
    std::vector<Entry> entries;
    juce::TableListBox table;
    juce::TextButton refreshBtn { "REFRESH" }, allBtn { "All" }, updatesBtn { "Updates" }, noneBtn { "None" },
                     installBtn { "INSTALL / UPDATE" }, removeBtn { "REMOVE" }, folderBtn { "OPEN FOLDER" }, updaterBtn;
    juce::Label status;
    double progress = 0.0;
    juce::ProgressBar progressBar { progress };
    juce::String updaterUrl;
    std::unique_ptr<Worker> worker;
    bool busy = false;
};

// ================================================================ application
class UpdaterApp : public juce::JUCEApplication
{
public:
    const juce::String getApplicationName() override { return JUCE_APPLICATION_NAME_STRING; }
    const juce::String getApplicationVersion() override { return JUCE_APPLICATION_VERSION_STRING; }
    bool moreThanOneInstanceAllowed() override { return false; }

    void initialise (const juce::String& args) override
    {
        if (args.contains ("--snapshot"))   // offline layout check: render a demo list to PNG and quit
        {
            UpdaterComponent c (true);
            auto img = c.createComponentSnapshot (c.getLocalBounds());
            juce::File out (args.fromFirstOccurrenceOf ("--snapshot", false, false).trim().unquoted());
            out.deleteFile();
            juce::FileOutputStream os (out);
            juce::PNGImageFormat().writeImageToStream (img, os);
            os.flush();
            quit();
            return;
        }
        window = std::make_unique<MainWindow>();
    }
    void shutdown() override { window.reset(); }
    void systemRequestedQuit() override { quit(); }

private:
    class MainWindow : public juce::DocumentWindow
    {
    public:
        MainWindow() : juce::DocumentWindow ("LiveTest Updater", theme::bg, juce::DocumentWindow::allButtons)
        {
            setUsingNativeTitleBar (true);
            setContentOwned (new UpdaterComponent(), true);
            setResizable (true, true);
            setResizeLimits (760, 460, 2000, 1400);
            centreWithSize (getWidth(), getHeight());
            setVisible (true);
        }
        void closeButtonPressed() override { juce::JUCEApplication::getInstance()->systemRequestedQuit(); }
    };
    std::unique_ptr<MainWindow> window;
};

START_JUCE_APPLICATION (UpdaterApp)
