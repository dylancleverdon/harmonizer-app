#pragma once

#include <functional>

#include <juce_core/juce_core.h>

#ifndef HARMONIZER_VERSION_CODE
#define HARMONIZER_VERSION_CODE 0
#endif
#ifndef HARMONIZER_VERSION_NAME
#define HARMONIZER_VERSION_NAME "dev"
#endif
#ifndef HARMONIZER_UPDATE_BASE
#define HARMONIZER_UPDATE_BASE \
    "https://github.com/dylancleverdon/harmonizer-app/releases/latest/download"
#endif
// Unlike HARMONIZER_UPDATE_BASE, which always points at whatever was published
// most recently, a build's own tagged release is never deleted -- this is what
// makes rolling back to an older version possible at all.
#ifndef HARMONIZER_RELEASES_BASE
#define HARMONIZER_RELEASES_BASE \
    "https://github.com/dylancleverdon/harmonizer-app/releases/download"
#endif

/**
 * Downloading and installing the plugin bundles.
 *
 * Shared by the in-plugin "Check for updates" button and the standalone Setup
 * app, so there is one implementation of the fiddly parts -- where plugins live
 * on each platform, replacing a bundle whose binary is currently loaded, and
 * clearing the quarantine flag macOS puts on anything from a browser.
 */
namespace harmonizer::install {

/** What the release page says the newest build is, for this platform. */
struct Manifest {
    bool valid = false;
    int versionCode = 0;
    juce::String versionName, notes, commit;
    juce::String assetFile, assetSha;
    juce::int64 assetSize = 0;
};

Manifest fetchManifest(juce::String& error);

/**
 * Every build gets its own permanent tagged release (see HARMONIZER_RELEASES_BASE
 * above), so a past version's manifest can be fetched the same way the latest
 * one can -- just from a different, never-deleted, address.
 */
Manifest fetchManifestForTag(const juce::String& tag, juce::String& error);

/** One entry per published build, newest first -- what "roll back" picks from. */
struct VersionEntry {
    int versionCode = 0;
    juce::String versionName, tag, notes, publishedAt;
};

/** The cumulative list of every build still available, newest first. */
juce::Array<VersionEntry> fetchVersionHistory(juce::String& error);

using Progress = std::function<void(double)>;
using ShouldStop = std::function<bool()>;

bool download(const juce::String& url, const juce::File& dest, juce::int64 expectedBytes,
              const Progress& onProgress, const ShouldStop& shouldStop, juce::String& error);

bool verifySha256(const juce::File& file, const juce::String& expected);
bool extract(const juce::File& archive, const juce::File& destDir, juce::String& error);

juce::String assetUrl(const juce::String& file);
juce::String assetUrlForTag(const juce::String& tag, const juce::String& file);

/** Per-user plug-in folders for this platform, in the order DAWs scan them. */
juce::Array<juce::File> pluginFolders();

/** The bundle the calling code is running from, if it is a plugin. */
juce::File runningBundle();

bool replaceBundle(const juce::File& installed, const juce::File& fresh, juce::String& error);

/** Sweeps up bundles moved aside by a previous in-place update. */
void cleanUpOldBundles();

struct InstallResult {
    int installed = 0;
    juce::StringArray paths;
    juce::String error;
};

/**
 * Installs every plugin format found in `staged` into the right folder for this
 * platform, replacing whatever is there.
 */
InstallResult installAll(const juce::File& staged);

/** Version recorded the last time something was installed, or 0. */
int installedVersionCode();
juce::String installedVersionName();
void recordInstalledVersion(int versionCode, const juce::String& versionName);

/** Whether each format is currently present on this machine. */
struct Presence {
    bool vst3 = false;
    bool audioUnit = false;
};
Presence whatIsInstalled();

}  // namespace harmonizer::install
