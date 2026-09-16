#pragma once

#include <juce_core/juce_core.h>

#include "HarmonizerInstall.h"

/**
 * Checks the project's releases page for a newer plugin build, downloads it, and
 * replaces the installed bundle in place.
 *
 * A plugin cannot restart itself the way an app can -- the host has it loaded --
 * so the install completes on disk and the user restarts their DAW. On Windows
 * the loaded binary cannot be deleted, but it *can* be renamed, which is what
 * makes replacing it while it runs possible at all.
 */
class PluginUpdater final : private juce::Thread {
public:
    enum class Stage {
        Idle,
        Checking,
        UpToDate,
        Available,
        Downloading,
        Installing,
        NeedsRestart,
        Failed
    };

    struct Status {
        Stage stage = Stage::Idle;
        juce::String message;
        juce::String availableVersion;
        juce::String notes;
        double progress = 0.0;
        juce::int64 sizeBytes = 0;

        // Version history, for rolling back. historyLoaded distinguishes "not
        // asked for yet" from "asked for, and it came back empty or failed" --
        // both otherwise look like an empty array.
        bool historyLoaded = false;
        juce::String historyError;
        juce::Array<harmonizer::install::VersionEntry> history;
    };

    PluginUpdater();
    ~PluginUpdater() override;

    void checkForUpdates();
    void downloadAndInstall();

    /** Every build still available, newest first -- background, like the rest. */
    void loadVersionHistory();

    /**
     * Installs a specific past build in place of whatever is running, using the
     * same download/verify/extract pipeline as an ordinary update -- just
     * pointed at that build's own permanent release instead of "latest".
     */
    void installVersion(const juce::String& tag);

    void reset();

    Status status() const;

    static juce::String currentVersionName() { return HARMONIZER_VERSION_NAME; }
    static int currentVersionCode() { return HARMONIZER_VERSION_CODE; }

    /** Bundle this binary is running from, or an invalid file if not found. */
    static juce::File runningBundle();

    /** Leftover ".old-" bundles from a previous update, removed on next launch. */
    static void cleanUpPreviousUpdate();

private:
    enum class Job { None, Check, Install, History, InstallVersion };

    void run() override;
    bool doCheck(juce::String& error);
    bool doInstall(juce::String& error);
    bool doLoadHistory();
    bool doInstallVersion(juce::String& error);
    bool performInstall(const juce::String& url, juce::String& error);

    void setStage(Stage stage, const juce::String& message);
    void setProgress(double progress);

    mutable juce::CriticalSection lock_;
    Status status_;
    Job job_ = Job::None;

    int availableCode_ = 0;
    juce::String assetFile_;
    juce::String assetSha_;
    juce::int64 assetSize_ = 0;
    juce::String pendingTag_;   // which past build installVersion() is fetching

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PluginUpdater)
};
