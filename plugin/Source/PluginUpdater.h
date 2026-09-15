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
    };

    PluginUpdater();
    ~PluginUpdater() override;

    void checkForUpdates();
    void downloadAndInstall();
    void reset();

    Status status() const;

    static juce::String currentVersionName() { return HARMONIZER_VERSION_NAME; }
    static int currentVersionCode() { return HARMONIZER_VERSION_CODE; }

    /** Bundle this binary is running from, or an invalid file if not found. */
    static juce::File runningBundle();

    /** Leftover ".old-" bundles from a previous update, removed on next launch. */
    static void cleanUpPreviousUpdate();

private:
    enum class Job { None, Check, Install };

    void run() override;
    bool doCheck(juce::String& error);
    bool doInstall(juce::String& error);

    void setStage(Stage stage, const juce::String& message);
    void setProgress(double progress);

    mutable juce::CriticalSection lock_;
    Status status_;
    Job job_ = Job::None;

    int availableCode_ = 0;
    juce::String assetFile_;
    juce::String assetSha_;
    juce::int64 assetSize_ = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PluginUpdater)
};
