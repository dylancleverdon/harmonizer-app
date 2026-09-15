#include "PluginUpdater.h"

#include "HarmonizerInstall.h"

namespace install = harmonizer::install;

PluginUpdater::PluginUpdater() : juce::Thread("HarmonizerUpdater") {}

PluginUpdater::~PluginUpdater() { stopThread(4000); }

PluginUpdater::Status PluginUpdater::status() const {
    const juce::ScopedLock sl(lock_);
    return status_;
}

void PluginUpdater::setStage(Stage stage, const juce::String& message) {
    const juce::ScopedLock sl(lock_);
    status_.stage = stage;
    status_.message = message;
}

void PluginUpdater::setProgress(double progress) {
    const juce::ScopedLock sl(lock_);
    status_.progress = progress;
}

void PluginUpdater::reset() {
    const juce::ScopedLock sl(lock_);
    status_ = Status{};
}

void PluginUpdater::checkForUpdates() {
    if (isThreadRunning()) return;
    job_ = Job::Check;
    setStage(Stage::Checking, "Checking for updates...");
    startThread();
}

void PluginUpdater::downloadAndInstall() {
    if (isThreadRunning()) return;
    job_ = Job::Install;
    setStage(Stage::Downloading, "Downloading...");
    setProgress(0.0);
    startThread();
}

void PluginUpdater::run() {
    juce::String error;
    const bool ok = (job_ == Job::Check) ? doCheck(error) : doInstall(error);
    if (!ok) setStage(Stage::Failed, error.isEmpty() ? "Something went wrong." : error);
    job_ = Job::None;
}

bool PluginUpdater::doCheck(juce::String& error) {
    const auto manifest = install::fetchManifest(error);
    if (!manifest.valid) return false;

    availableCode_ = manifest.versionCode;
    assetFile_ = manifest.assetFile;
    assetSha_ = manifest.assetSha;
    assetSize_ = manifest.assetSize;

    const juce::ScopedLock sl(lock_);
    status_.availableVersion = manifest.versionName;
    status_.notes = manifest.notes;
    status_.sizeBytes = manifest.assetSize;
    if (manifest.versionCode > currentVersionCode()) {
        status_.stage = Stage::Available;
        status_.message = "Version " + manifest.versionName + " is available.";
    } else {
        status_.stage = Stage::UpToDate;
        status_.message = "Up to date.";
    }
    return true;
}

bool PluginUpdater::doInstall(juce::String& error) {
    if (assetFile_.isEmpty()) {
        error = "Check for updates first.";
        return false;
    }

    const auto temp = juce::File::getSpecialLocation(juce::File::tempDirectory)
                          .getChildFile("harmonizer-update");
    temp.createDirectory();
    const auto archive = temp.getChildFile(assetFile_);

    if (!install::download(install::assetUrl(assetFile_), archive, assetSize_,
                           [this](double p) { setProgress(p); },
                           [this] { return threadShouldExit(); }, error)) {
        return false;
    }

    if (!install::verifySha256(archive, assetSha_)) {
        archive.deleteFile();
        error = "The download did not match its published checksum.";
        return false;
    }

    setStage(Stage::Installing, "Installing...");

    const auto staged = temp.getChildFile("staged");
    if (!install::extract(archive, staged, error)) return false;

    auto result = install::installAll(staged);

    // The bundle this code is running from may sit outside the standard folders
    // -- someone can put a plugin anywhere -- so make sure it is updated too.
    if (const auto running = install::runningBundle(); running.isDirectory()) {
        if (!result.paths.contains(running.getFullPathName())) {
            const auto fresh = staged.getChildFile(running.getFileName());
            juce::String reason;
            if (install::replaceBundle(running, fresh, reason)) {
                ++result.installed;
                result.paths.add(running.getFullPathName());
            }
        }
    }

    archive.deleteFile();
    staged.deleteRecursively();

    if (result.installed == 0) {
        error = result.error.isEmpty() ? "Nothing could be installed." : result.error;
        return false;
    }

    install::recordInstalledVersion(availableCode_, status().availableVersion);
    setStage(Stage::NeedsRestart,
             "Installed version " + status().availableVersion +
                 ". Quit your DAW completely and reopen it to load it.");
    return true;
}

juce::File PluginUpdater::runningBundle() { return install::runningBundle(); }

void PluginUpdater::cleanUpPreviousUpdate() { install::cleanUpOldBundles(); }
