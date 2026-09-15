// Harmonizer Setup -- installs the plugin and keeps it up to date.
//
// Designed to be the only thing a non-technical person has to run: it works out
// what is already installed, fetches the right build for this machine, puts the
// files where DAWs look for them, and then explains how to get MIDI into the
// plugin for whichever DAWs they use. Keeping it installed turns every future
// update into "open this, press one button".

#include <juce_gui_extra/juce_gui_extra.h>

#include "DawGuides.h"
#include "HarmonizerInstall.h"

namespace install = harmonizer::install;
namespace guides = harmonizer::guides;

namespace {

const juce::Colour kBackground{0xff0e1113};
const juce::Colour kSurface{0xff171b1e};
const juce::Colour kAccent{0xff4dd0c0};
const juce::Colour kText{0xffe3e6e8};
const juce::Colour kMuted{0xffa8b4b8};
const juce::Colour kWarn{0xffe8a33d};

/**
 * Copies this app somewhere permanent so it can be found again later. Running
 * the updater out of the Downloads folder works until someone tidies up.
 */
juce::String installSelf() {
    const auto self = juce::File::getSpecialLocation(juce::File::currentApplicationFile);

#if JUCE_MAC
    const auto apps = juce::File::getSpecialLocation(juce::File::userHomeDirectory)
                          .getChildFile("Applications");
    apps.createDirectory();
    const auto target = apps.getChildFile(self.getFileName());
    if (target == self) return target.getFullPathName();
    target.deleteRecursively();
    if (!self.copyDirectoryTo(target)) return {};
    return target.getFullPathName();
#elif JUCE_WINDOWS
    const auto dir = juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
                         .getChildFile("Programs/Harmonizer");
    dir.createDirectory();
    const auto target = dir.getChildFile(self.getFileName());
    if (target == self) return target.getFullPathName();
    if (!self.copyFileTo(target)) return {};

    // A Start Menu entry so it can be found by name rather than by path.
    const auto startMenu =
        juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
            .getChildFile("Microsoft/Windows/Start Menu/Programs");
    if (startMenu.isDirectory()) {
        const auto link = startMenu.getChildFile("Harmonizer Setup.lnk");
        juce::ChildProcess ps;
        ps.start("powershell -NoProfile -Command \"$s=(New-Object -COM WScript.Shell)"
                 ".CreateShortcut('" + link.getFullPathName() + "'); $s.TargetPath='" +
                 target.getFullPathName() + "'; $s.Save()\"");
        ps.waitForProcessToFinish(15000);
    }
    return target.getFullPathName();
#else
    return self.getFullPathName();
#endif
}

}  // namespace

// ---------------------------------------------------------------------------

class SetupComponent final : public juce::Component,
                             private juce::Timer,
                             private juce::Thread {
public:
    SetupComponent() : juce::Thread("HarmonizerSetup") {
        title_.setText("Harmonizer Setup", juce::dontSendNotification);
        title_.setFont(juce::FontOptions(24.0f, juce::Font::bold));
        title_.setColour(juce::Label::textColourId, kText);
        addAndMakeVisible(title_);

        subtitle_.setFont(juce::FontOptions(13.0f));
        subtitle_.setColour(juce::Label::textColourId, kMuted);
        addAndMakeVisible(subtitle_);

        prompt_.setText("Which do you use? This only changes the instructions at the end --"
                        " everything gets installed either way.",
                        juce::dontSendNotification);
        prompt_.setFont(juce::FontOptions(12.0f));
        prompt_.setColour(juce::Label::textColourId, kMuted);
        prompt_.setJustificationType(juce::Justification::topLeft);
        addAndMakeVisible(prompt_);

        for (int i = 0; i < guides::kNumDaws; ++i) {
            const auto& daw = guides::kDaws[i];
           #if !JUCE_MAC
            if (daw.macOnly) continue;
           #endif
            auto* box = boxes_.add(new juce::ToggleButton(daw.name));
            box->setColour(juce::ToggleButton::textColourId, kText);
            box->setColour(juce::ToggleButton::tickColourId, kAccent);
            box->onClick = [this] { refreshButtons(); };
            addAndMakeVisible(*box);
            boxKeys_.add(daw.key);
        }

        action_.setButtonText("Install");
        action_.onClick = [this] { startJob(Job::Install); };
        addAndMakeVisible(action_);

        recheck_.setButtonText("Check again");
        recheck_.onClick = [this] { startJob(Job::Check); };
        addAndMakeVisible(recheck_);

        detail_.setMultiLine(true);
        detail_.setReadOnly(true);
        detail_.setScrollbarsShown(true);
        detail_.setCaretVisible(false);
        detail_.setFont(juce::FontOptions(juce::Font::getDefaultMonospacedFontName(), 12.0f,
                                          juce::Font::plain));
        detail_.setColour(juce::TextEditor::backgroundColourId, kSurface);
        detail_.setColour(juce::TextEditor::textColourId, kText);
        detail_.setColour(juce::TextEditor::outlineColourId, juce::Colours::transparentBlack);
        addAndMakeVisible(detail_);

        addChildComponent(bar_);

        setSize(680, 620);
        install::cleanUpOldBundles();
        refreshHeader();
        startTimerHz(8);
        startJob(Job::Check);
    }

    ~SetupComponent() override {
        stopTimer();
        stopThread(6000);
    }

    void paint(juce::Graphics& g) override {
        g.fillAll(kBackground);
        g.setColour(kSurface);
        g.fillRoundedRectangle(juce::Rectangle<int>(16, 92, getWidth() - 32, 128).toFloat(), 8.0f);
    }

    void resized() override {
        title_.setBounds(20, 14, 400, 34);
        subtitle_.setBounds(20, 48, getWidth() - 40, 40);

        prompt_.setBounds(28, 100, getWidth() - 56, 32);
        int y = 138;
        int x = 28;
        for (auto* box : boxes_) {
            box->setBounds(x, y, 200, 26);
            x += 210;
            if (x > getWidth() - 200) { x = 28; y += 30; }
        }

        action_.setBounds(20, 236, 220, 38);
        recheck_.setBounds(252, 236, 130, 38);
        bar_.setBounds(396, 242, getWidth() - 416, 26);

        detail_.setBounds(16, 288, getWidth() - 32, getHeight() - 308);
    }

private:
    enum class Job { None, Check, Install };

    void startJob(Job job) {
        if (isThreadRunning()) return;
        job_ = job;
        progress_ = 0.0;
        {
            const juce::ScopedLock sl(lock_);
            busyMessage_ = (job == Job::Check) ? "Checking for the latest version..."
                                               : "Downloading...";
            failed_.clear();
        }
        startThread();
    }

    void run() override {
        juce::String error;
        if (job_ == Job::Check) {
            const auto m = install::fetchManifest(error);
            const juce::ScopedLock sl(lock_);
            manifest_ = m;
            failed_ = m.valid ? juce::String() : error;
        } else {
            doInstall(error);
            const juce::ScopedLock sl(lock_);
            failed_ = error;
        }
        job_ = Job::None;
        {
            const juce::ScopedLock sl(lock_);
            busyMessage_.clear();
        }
    }

    void doInstall(juce::String& error) {
        install::Manifest m;
        {
            const juce::ScopedLock sl(lock_);
            m = manifest_;
        }
        if (!m.valid) {
            m = install::fetchManifest(error);
            if (!m.valid) return;
            const juce::ScopedLock sl(lock_);
            manifest_ = m;
        }

        const auto temp = juce::File::getSpecialLocation(juce::File::tempDirectory)
                              .getChildFile("harmonizer-setup");
        temp.createDirectory();
        const auto archive = temp.getChildFile(m.assetFile);

        if (!install::download(install::assetUrl(m.assetFile), archive, m.assetSize,
                               [this](double p) { progress_ = p; },
                               [this] { return threadShouldExit(); }, error)) {
            return;
        }

        if (!install::verifySha256(archive, m.assetSha)) {
            archive.deleteFile();
            error = "The download did not match its published checksum. Try again.";
            return;
        }

        {
            const juce::ScopedLock sl(lock_);
            busyMessage_ = "Installing...";
        }

        const auto staged = temp.getChildFile("staged");
        if (!install::extract(archive, staged, error)) return;

        auto result = install::installAll(staged);
        archive.deleteFile();
        staged.deleteRecursively();

        if (result.installed == 0) {
            error = result.error.isEmpty() ? "Nothing could be installed." : result.error;
            return;
        }

        install::recordInstalledVersion(m.versionCode, m.versionName);

        const juce::ScopedLock sl(lock_);
        installedPaths_ = result.paths;
        selfPath_ = installSelf();
        justInstalled_ = true;
    }

    void timerCallback() override {
        refreshHeader();
        refreshButtons();

        juce::String busy;
        {
            const juce::ScopedLock sl(lock_);
            busy = busyMessage_;
        }
        bar_.setVisible(busy.isNotEmpty() && job_ == Job::Install);
        buildDetailText();
    }

    void refreshHeader() {
        const int installedCode = install::installedVersionCode();
        const auto presence = install::whatIsInstalled();

        install::Manifest m;
        juce::String failed;
        {
            const juce::ScopedLock sl(lock_);
            m = manifest_;
            failed = failed_;
        }

        juce::String s;
        if (!presence.vst3 && !presence.audioUnit) {
            s << "Harmonizer is not installed on this computer yet.";
        } else {
            s << "Installed: " << install::installedVersionName();
            if (installedCode == 0) s = "Installed (version unknown)";
        }
        if (m.valid) {
            s << "     Latest available: " << m.versionName;
            if (m.versionCode > installedCode) s << "  -- update ready";
        } else if (failed.isNotEmpty()) {
            s << "\n" << failed;
        }
        subtitle_.setText(s, juce::dontSendNotification);
        subtitle_.setColour(juce::Label::textColourId, failed.isNotEmpty() ? kWarn : kMuted);
    }

    void refreshButtons() {
        const bool busy = isThreadRunning();
        action_.setEnabled(!busy);
        recheck_.setEnabled(!busy);

        install::Manifest m;
        {
            const juce::ScopedLock sl(lock_);
            m = manifest_;
        }
        const int installedCode = install::installedVersionCode();
        const auto presence = install::whatIsInstalled();

        if (busy) {
            action_.setButtonText(job_ == Job::Install ? "Working..." : "Checking...");
        } else if (!presence.vst3 && !presence.audioUnit) {
            action_.setButtonText("Install Harmonizer");
        } else if (m.valid && m.versionCode > installedCode) {
            action_.setButtonText("Update to " + m.versionName);
        } else {
            action_.setButtonText("Reinstall");
        }
    }

    void buildDetailText() {
        juce::StringArray selected;
        for (int i = 0; i < boxes_.size(); ++i) {
            if (boxes_[i]->getToggleState()) selected.add(boxKeys_[i]);
        }

        bool done;
        juce::StringArray paths;
        juce::String failed, selfPath;
        install::Manifest m;
        {
            const juce::ScopedLock sl(lock_);
            done = justInstalled_;
            paths = installedPaths_;
            failed = failed_;
            selfPath = selfPath_;
            m = manifest_;
        }

        const auto key = selected.joinIntoString(",") + "|" + juce::String(done ? 1 : 0) + "|" +
                         failed + "|" + juce::String(paths.size());
        if (key == lastDetailKey_) return;
        lastDetailKey_ = key;

        juce::String t;
        if (failed.isNotEmpty()) {
            t << "Something went wrong\n"
              << "---------------------\n"
              << failed << "\n\n"
              << "If a DAW is open, close it completely and press the button again.\n";
        } else if (done) {
            t << "Installed\n"
              << "---------\n";
            for (const auto& p : paths) t << "  " << p << "\n";
            if (selfPath.isNotEmpty()) {
                t << "\nThis installer was copied to:\n  " << selfPath
                  << "\nOpen it any time to update -- that is the whole update process.\n";
            }
            t << "\nNow quit your DAW completely and reopen it so it rescans.\n";
        } else {
            t << "What this does\n"
              << "--------------\n"
              << "Downloads the current build and installs it where your DAWs look:\n";
           #if JUCE_MAC
            t << "  Audio Unit  ->  ~/Library/Audio/Plug-Ins/Components\n"
              << "  VST3        ->  ~/Library/Audio/Plug-Ins/VST3\n"
              << "\nLogic only loads Audio Units; everything else uses the VST3. Both go on,\n"
                 "so whichever you use later already works.\n";
           #else
            t << "  VST3  ->  Common Files\\VST3 (or your personal VST3 folder)\n";
           #endif
            t << "\nIt also copies itself somewhere permanent, so future updates are one\n"
                 "button in this window.\n";
        }

        if (!selected.isEmpty()) {
            t << "\n\nGetting MIDI into it\n"
              << "--------------------\n"
              << "Harmonizer is an audio effect that listens for notes, not an instrument.\n"
              << "The audio is your voice or horn; the notes choose the harmonies.\n";
            for (int i = 0; i < guides::kNumDaws; ++i) {
                const auto& daw = guides::kDaws[i];
                if (!selected.contains(daw.key)) continue;
                t << "\n" << daw.name << "\n";
                t << juce::String::repeatedString("-", juce::String(daw.name).length()) << "\n";
                t << daw.steps << "\n";
            }
            t << "\nStuck? Open the plugin window and play some notes. The voice counter\n"
                 "tells you whether MIDI is arriving: if it stays at 0, the notes are not\n"
                 "reaching the plugin and the routing is the thing to fix.\n";
        } else {
            t << "\nTick your DAWs above to get the MIDI routing steps for each one.\n";
        }

        detail_.setText(t, false);
    }

    juce::Label title_, subtitle_, prompt_;
    juce::OwnedArray<juce::ToggleButton> boxes_;
    juce::StringArray boxKeys_;
    juce::TextButton action_, recheck_;
    juce::TextEditor detail_;
    double progress_ = 0.0;
    juce::ProgressBar bar_{progress_};

    juce::CriticalSection lock_;
    install::Manifest manifest_;
    juce::String busyMessage_, failed_, selfPath_, lastDetailKey_;
    juce::StringArray installedPaths_;
    bool justInstalled_ = false;
    std::atomic<Job> job_{Job::None};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SetupComponent)
};

// ---------------------------------------------------------------------------

class SetupWindow final : public juce::DocumentWindow {
public:
    SetupWindow()
        : DocumentWindow("Harmonizer Setup", kBackground, DocumentWindow::allButtons) {
        setUsingNativeTitleBar(true);
        setContentOwned(new SetupComponent(), true);
        setResizable(true, true);
        centreWithSize(getWidth(), getHeight());
        setVisible(true);
    }

    void closeButtonPressed() override {
        juce::JUCEApplication::getInstance()->systemRequestedQuit();
    }
};

class SetupApplication final : public juce::JUCEApplication {
public:
    const juce::String getApplicationName() override { return "Harmonizer Setup"; }
    const juce::String getApplicationVersion() override { return HARMONIZER_VERSION_NAME; }
    bool moreThanOneInstanceAllowed() override { return false; }

    void initialise(const juce::String&) override { window_ = std::make_unique<SetupWindow>(); }
    void shutdown() override { window_ = nullptr; }

private:
    std::unique_ptr<SetupWindow> window_;
};

START_JUCE_APPLICATION(SetupApplication)
