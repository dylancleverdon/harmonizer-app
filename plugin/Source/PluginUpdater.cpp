#include "PluginUpdater.h"

#include <juce_cryptography/juce_cryptography.h>

namespace {

#if JUCE_MAC
constexpr const char* kPlatformKey = "macos";
#elif JUCE_WINDOWS
constexpr const char* kPlatformKey = "windows";
#else
constexpr const char* kPlatformKey = "linux";
#endif

constexpr const char* kOldSuffix = ".old-";

juce::String manifestUrl() {
    return juce::String(HARMONIZER_UPDATE_BASE) + "/plugin-version.json";
}

juce::String assetUrl(const juce::String& file) {
    return juce::String(HARMONIZER_UPDATE_BASE) + "/" + file;
}

bool isBundleName(const juce::String& name) {
    return name.endsWithIgnoreCase(".vst3") || name.endsWithIgnoreCase(".component");
}

/** Directories a DAW is likely to have the plugin installed in. */
juce::Array<juce::File> standardPluginFolders() {
    juce::Array<juce::File> folders;
    const auto home = juce::File::getSpecialLocation(juce::File::userHomeDirectory);

#if JUCE_MAC
    folders.add(home.getChildFile("Library/Audio/Plug-Ins/VST3"));
    folders.add(home.getChildFile("Library/Audio/Plug-Ins/Components"));
#elif JUCE_WINDOWS
    if (auto common = juce::SystemStats::getEnvironmentVariable("CommonProgramFiles", {});
        common.isNotEmpty()) {
        folders.add(juce::File(common).getChildFile("VST3"));
    }
    folders.add(juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
                    .getChildFile("Programs/Common/VST3"));
#else
    folders.add(home.getChildFile(".vst3"));
#endif
    return folders;
}

}  // namespace

PluginUpdater::PluginUpdater() : juce::Thread("HarmonizerUpdater") {}

PluginUpdater::~PluginUpdater() {
    stopThread(4000);
}

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
    const juce::URL url(manifestUrl());
    auto stream = url.createInputStream(
        juce::URL::InputStreamOptions(juce::URL::ParameterHandling::inAddress)
            .withConnectionTimeoutMs(15000)
            .withNumRedirectsToFollow(5));

    if (stream == nullptr) {
        error = "Could not reach the releases page.";
        return false;
    }

    const auto text = stream->readEntireStreamAsString();
    const auto json = juce::JSON::parse(text);
    if (!json.isObject()) {
        error = "The release manifest could not be read.";
        return false;
    }

    availableCode_ = static_cast<int>(json.getProperty("versionCode", 0));
    const auto versionName = json.getProperty("versionName", "unknown").toString();
    const auto notes = json.getProperty("notes", "").toString();

    const auto assets = json.getProperty("assets", juce::var());
    const auto platform = assets.getProperty(kPlatformKey, juce::var());
    if (!platform.isObject()) {
        error = juce::String("No build published for this platform (") + kPlatformKey + ").";
        return false;
    }

    assetFile_ = platform.getProperty("file", "").toString();
    assetSha_ = platform.getProperty("sha256", "").toString();
    assetSize_ = static_cast<juce::int64>(
        static_cast<double>(platform.getProperty("sizeBytes", 0)));

    if (assetFile_.isEmpty()) {
        error = "The release manifest has no download for this platform.";
        return false;
    }

    const juce::ScopedLock sl(lock_);
    status_.availableVersion = versionName;
    status_.notes = notes;
    status_.sizeBytes = assetSize_;
    if (availableCode_ > currentVersionCode()) {
        status_.stage = Stage::Available;
        status_.message = "Version " + versionName + " is available.";
    } else {
        status_.stage = Stage::UpToDate;
        status_.message = "Up to date.";
    }
    return true;
}

bool PluginUpdater::downloadTo(const juce::String& url, const juce::File& dest,
                               juce::int64 expectedBytes, juce::String& error) {
    const juce::URL source(url);
    auto stream = source.createInputStream(
        juce::URL::InputStreamOptions(juce::URL::ParameterHandling::inAddress)
            .withConnectionTimeoutMs(15000)
            .withNumRedirectsToFollow(5));
    if (stream == nullptr) {
        error = "Could not start the download.";
        return false;
    }

    dest.deleteFile();
    juce::FileOutputStream out(dest);
    if (out.failedToOpen()) {
        error = "Could not write to the temporary folder.";
        return false;
    }

    juce::HeapBlock<char> buffer(64 * 1024);
    juce::int64 total = 0;
    while (!threadShouldExit()) {
        const int read = stream->read(buffer.getData(), 64 * 1024);
        if (read <= 0) break;
        out.write(buffer.getData(), static_cast<size_t>(read));
        total += read;
        if (expectedBytes > 0) {
            setProgress(juce::jlimit(0.0, 1.0, static_cast<double>(total) /
                                                    static_cast<double>(expectedBytes)));
        }
    }
    out.flush();

    if (threadShouldExit()) {
        error = "Cancelled.";
        return false;
    }
    if (total <= 0) {
        error = "The download was empty.";
        return false;
    }
    return true;
}

bool PluginUpdater::extract(const juce::File& archive, const juce::File& destDir,
                            juce::String& error) {
    destDir.deleteRecursively();
    if (!destDir.createDirectory()) {
        error = "Could not create a staging folder.";
        return false;
    }

#if JUCE_MAC
    // ditto preserves the executable bit and bundle metadata. JUCE's ZipFile
    // does not restore permissions, and a plugin binary without +x will not
    // load, so this matters rather than being a nicety.
    juce::ChildProcess ditto;
    if (ditto.start(juce::StringArray{"/usr/bin/ditto", "-x", "-k",
                                      archive.getFullPathName(),
                                      destDir.getFullPathName()})) {
        ditto.waitForProcessToFinish(120000);
        if (ditto.getExitCode() == 0) return true;
    }
    error = "Could not unpack the download.";
    return false;
#else
    juce::ZipFile zip(archive);
    const auto result = zip.uncompressTo(destDir, true);
    if (result.failed()) {
        error = result.getErrorMessage();
        return false;
    }
    return true;
#endif
}

juce::File PluginUpdater::runningBundle() {
    auto file = juce::File::getSpecialLocation(juce::File::currentExecutableFile);
    for (int i = 0; i < 6; ++i) {
        if (!file.exists()) break;
        // On Windows the loaded DLL is itself named ".vst3", so only a directory
        // counts as the bundle root.
        if (isBundleName(file.getFileName()) && file.isDirectory()) return file;
        const auto parent = file.getParentDirectory();
        if (parent == file) break;
        file = parent;
    }
    return {};
}

juce::Array<juce::File> PluginUpdater::installTargets() {
    juce::Array<juce::File> targets;

    if (const auto running = runningBundle(); running.isDirectory()) {
        targets.add(running);
    }

    // Also refresh the sibling format if it is installed, so the AU and the
    // VST3 do not drift to different versions.
    for (const auto& folder : standardPluginFolders()) {
        if (!folder.isDirectory()) continue;
        for (const auto& name : {"Harmonizer.vst3", "Harmonizer.component"}) {
            const auto candidate = folder.getChildFile(name);
            if (candidate.isDirectory()) targets.addIfNotAlreadyThere(candidate);
        }
    }
    return targets;
}

bool PluginUpdater::replaceBundle(const juce::File& installed, const juce::File& fresh,
                                  juce::String& error) {
    if (!fresh.isDirectory()) {
        error = "The download did not contain " + installed.getFileName() + ".";
        return false;
    }

    juce::File moved;
    if (installed.exists()) {
        // Move the old one aside rather than deleting it. The binary inside is
        // loaded right now: on Windows it cannot be deleted, but it can be
        // renamed, and on macOS the open handle keeps working from the moved
        // copy until the host lets go.
        const auto stamp = juce::String(juce::Time::getCurrentTime().toMilliseconds());
        moved = installed.getSiblingFile(installed.getFileName() + kOldSuffix + stamp);
        if (!installed.moveFileTo(moved)) {
            error = "Could not move the installed plugin aside. Close your DAW and try again.";
            return false;
        }
    }

    if (!fresh.copyDirectoryTo(installed)) {
        // Put things back rather than leaving nothing installed.
        if (moved.exists()) moved.moveFileTo(installed);
        error = "Could not write the new plugin to " + installed.getFullPathName() + ".";
        return false;
    }

#if JUCE_MAC
    // Files written by this process are not quarantined, but clear the flag
    // anyway in case the archive carried one over.
    juce::ChildProcess xattr;
    if (xattr.start(juce::StringArray{"/usr/bin/xattr", "-dr", "com.apple.quarantine",
                                      installed.getFullPathName()})) {
        xattr.waitForProcessToFinish(10000);
    }
#endif
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

    if (!downloadTo(assetUrl(assetFile_), archive, assetSize_, error)) return false;

    if (assetSha_.isNotEmpty()) {
        juce::FileInputStream in(archive);
        const juce::SHA256 hash(in);
        if (!hash.toHexString().equalsIgnoreCase(assetSha_)) {
            archive.deleteFile();
            error = "The download did not match its published checksum.";
            return false;
        }
    }

    setStage(Stage::Installing, "Installing...");

    const auto staging = temp.getChildFile("staged");
    if (!extract(archive, staging, error)) return false;

    const auto targets = installTargets();
    if (targets.isEmpty()) {
        error = "Could not find where the plugin is installed.";
        return false;
    }

    int installed = 0;
    for (const auto& target : targets) {
        const auto fresh = staging.getChildFile(target.getFileName());
        juce::String reason;
        if (replaceBundle(target, fresh, reason)) {
            ++installed;
        } else if (error.isEmpty()) {
            error = reason;
        }
    }

    archive.deleteFile();
    staging.deleteRecursively();

    if (installed == 0) return false;

    error.clear();
    setStage(Stage::NeedsRestart,
             juce::String("Installed. Quit and reopen your DAW to load version ") +
                 status().availableVersion + ".");
    return true;
}

void PluginUpdater::cleanUpPreviousUpdate() {
    juce::Array<juce::File> folders = standardPluginFolders();
    if (const auto running = runningBundle(); running.isDirectory()) {
        folders.addIfNotAlreadyThere(running.getParentDirectory());
    }

    for (const auto& folder : folders) {
        if (!folder.isDirectory()) continue;
        for (const auto& item : folder.findChildFiles(
                 juce::File::findFilesAndDirectories, false, "*" + juce::String(kOldSuffix) + "*")) {
            item.deleteRecursively();   // best effort; still loaded means next time
        }
    }
}
