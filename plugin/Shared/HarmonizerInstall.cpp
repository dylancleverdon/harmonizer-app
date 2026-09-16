#include "HarmonizerInstall.h"

#include <juce_cryptography/juce_cryptography.h>

namespace harmonizer::install {
namespace {

#if JUCE_MAC
constexpr const char* kPlatformKey = "macos";
#elif JUCE_WINDOWS
constexpr const char* kPlatformKey = "windows";
#else
constexpr const char* kPlatformKey = "linux";
#endif

constexpr const char* kOldSuffix = ".old-";

bool isBundleName(const juce::String& name) {
    return name.endsWithIgnoreCase(".vst3") || name.endsWithIgnoreCase(".component");
}

juce::File supportDir() {
    auto dir = juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
                   .getChildFile("Harmonizer");
    dir.createDirectory();
    return dir;
}

std::unique_ptr<juce::InputStream> open(const juce::String& url) {
    return juce::URL(url).createInputStream(
        juce::URL::InputStreamOptions(juce::URL::ParameterHandling::inAddress)
            .withConnectionTimeoutMs(20000)
            .withNumRedirectsToFollow(6));
}

}  // namespace

juce::String assetUrl(const juce::String& file) {
    return juce::String(HARMONIZER_UPDATE_BASE) + "/" + file;
}

juce::String assetUrlForTag(const juce::String& tag, const juce::String& file) {
    return juce::String(HARMONIZER_RELEASES_BASE) + "/" + tag + "/" + file;
}

namespace {

Manifest parseManifest(const juce::var& json, juce::String& error) {
    Manifest m;
    if (!json.isObject()) {
        error = "The download page returned something unexpected.";
        return m;
    }

    m.versionCode = static_cast<int>(json.getProperty("versionCode", 0));
    m.versionName = json.getProperty("versionName", "unknown").toString();
    m.notes = json.getProperty("notes", "").toString();
    m.commit = json.getProperty("commit", "").toString();

    const auto platform = json.getProperty("assets", juce::var())
                              .getProperty(kPlatformKey, juce::var());
    if (!platform.isObject()) {
        error = juce::String("No build has been published for ") + kPlatformKey + " yet.";
        return m;
    }

    m.assetFile = platform.getProperty("file", "").toString();
    m.assetSha = platform.getProperty("sha256", "").toString();
    m.assetSize = static_cast<juce::int64>(
        static_cast<double>(platform.getProperty("sizeBytes", 0)));

    if (m.assetFile.isEmpty()) {
        error = "The download page has no file for this system.";
        return m;
    }
    m.valid = true;
    return m;
}

}  // namespace

Manifest fetchManifest(juce::String& error) {
    auto stream = open(juce::String(HARMONIZER_UPDATE_BASE) + "/plugin-version.json");
    if (stream == nullptr) {
        error = "Could not reach the download page. Check your internet connection.";
        return {};
    }
    return parseManifest(juce::JSON::parse(stream->readEntireStreamAsString()), error);
}

Manifest fetchManifestForTag(const juce::String& tag, juce::String& error) {
    auto stream = open(assetUrlForTag(tag, "plugin-version.json"));
    if (stream == nullptr) {
        error = "Could not reach the download page. Check your internet connection.";
        return {};
    }
    return parseManifest(juce::JSON::parse(stream->readEntireStreamAsString()), error);
}

juce::Array<VersionEntry> fetchVersionHistory(juce::String& error) {
    juce::Array<VersionEntry> out;
    auto stream = open(juce::String(HARMONIZER_UPDATE_BASE) + "/versions.json");
    if (stream == nullptr) {
        error = "Could not reach the download page. Check your internet connection.";
        return out;
    }

    const auto json = juce::JSON::parse(stream->readEntireStreamAsString());
    if (!json.isArray()) {
        error = "The version history could not be read.";
        return out;
    }

    for (const auto& item : *json.getArray()) {
        if (!item.isObject()) continue;
        VersionEntry e;
        e.versionCode = static_cast<int>(item.getProperty("versionCode", 0));
        e.versionName = item.getProperty("versionName", "unknown").toString();
        e.tag = item.getProperty("tag", "").toString();
        e.notes = item.getProperty("notes", "").toString();
        e.publishedAt = item.getProperty("publishedAt", "").toString();
        if (e.tag.isNotEmpty()) out.add(e);
    }
    return out;
}

bool download(const juce::String& url, const juce::File& dest, juce::int64 expectedBytes,
              const Progress& onProgress, const ShouldStop& shouldStop, juce::String& error) {
    auto stream = open(url);
    if (stream == nullptr) {
        error = "The download could not be started.";
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
    while (!(shouldStop && shouldStop())) {
        const int read = stream->read(buffer.getData(), 64 * 1024);
        if (read <= 0) break;
        out.write(buffer.getData(), static_cast<size_t>(read));
        total += read;
        if (onProgress && expectedBytes > 0) {
            onProgress(juce::jlimit(0.0, 1.0, static_cast<double>(total) /
                                                  static_cast<double>(expectedBytes)));
        }
    }
    out.flush();

    if (shouldStop && shouldStop()) { error = "Cancelled."; return false; }
    if (total <= 0) { error = "The download came back empty."; return false; }
    return true;
}

bool verifySha256(const juce::File& file, const juce::String& expected) {
    if (expected.isEmpty()) return true;
    juce::FileInputStream in(file);
    if (in.failedToOpen()) return false;
    return juce::SHA256(in).toHexString().equalsIgnoreCase(expected);
}

bool extract(const juce::File& archive, const juce::File& destDir, juce::String& error) {
    destDir.deleteRecursively();
    if (!destDir.createDirectory()) {
        error = "Could not create a temporary folder.";
        return false;
    }

#if JUCE_MAC
    // ditto preserves the executable bit; JUCE's ZipFile does not restore
    // permissions, and a plugin binary without +x simply will not load.
    juce::ChildProcess ditto;
    if (ditto.start(juce::StringArray{"/usr/bin/ditto", "-x", "-k",
                                      archive.getFullPathName(), destDir.getFullPathName()})) {
        ditto.waitForProcessToFinish(180000);
        if (ditto.getExitCode() == 0) return true;
    }
    error = "Could not unpack the download.";
    return false;
#else
    juce::ZipFile zip(archive);
    const auto result = zip.uncompressTo(destDir, true);
    if (result.failed()) { error = result.getErrorMessage(); return false; }
    return true;
#endif
}

juce::Array<juce::File> pluginFolders() {
    juce::Array<juce::File> folders;
    const auto home = juce::File::getSpecialLocation(juce::File::userHomeDirectory);

#if JUCE_MAC
    folders.add(home.getChildFile("Library/Audio/Plug-Ins/VST3"));
    folders.add(home.getChildFile("Library/Audio/Plug-Ins/Components"));
#elif JUCE_WINDOWS
    const auto common = juce::SystemStats::getEnvironmentVariable("CommonProgramFiles", {});
    if (common.isNotEmpty()) folders.add(juce::File(common).getChildFile("VST3"));
    folders.add(juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
                    .getChildFile("Programs/Common/VST3"));
#else
    folders.add(home.getChildFile(".vst3"));
#endif
    return folders;
}

juce::File runningBundle() {
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

bool replaceBundle(const juce::File& installed, const juce::File& fresh, juce::String& error) {
    if (!fresh.isDirectory()) {
        error = "The download did not contain " + installed.getFileName() + ".";
        return false;
    }

    juce::File movedAside;
    if (installed.exists()) {
        // Move the old one out of the way rather than deleting it. Its binary may
        // be loaded by a running DAW right now: on Windows a loaded file cannot
        // be deleted, but it can be renamed, which is what makes replacing a
        // live plugin possible at all.
        const auto stamp = juce::String(juce::Time::getCurrentTime().toMilliseconds());
        movedAside = installed.getSiblingFile(installed.getFileName() + kOldSuffix + stamp);
        if (!installed.moveFileTo(movedAside)) {
            error = "Could not replace " + installed.getFileName() +
                    ". Close your DAW completely and try again.";
            return false;
        }
    }

    if (!fresh.copyDirectoryTo(installed)) {
        if (movedAside.exists()) movedAside.moveFileTo(installed);   // put it back
        error = "Could not write to " + installed.getParentDirectory().getFullPathName() + ".";
        return false;
    }

#if JUCE_MAC
    // Files this process writes are not quarantined, but an archive can carry
    // the flag across. A quarantined plugin will not load at all.
    juce::ChildProcess xattr;
    if (xattr.start(juce::StringArray{"/usr/bin/xattr", "-dr", "com.apple.quarantine",
                                      installed.getFullPathName()})) {
        xattr.waitForProcessToFinish(15000);
    }
#endif
    return true;
}

void cleanUpOldBundles() {
    auto folders = pluginFolders();
    if (const auto running = runningBundle(); running.isDirectory()) {
        folders.addIfNotAlreadyThere(running.getParentDirectory());
    }
    for (const auto& folder : folders) {
        if (!folder.isDirectory()) continue;
        const auto pattern = juce::String("*") + kOldSuffix + "*";
        for (const auto& item :
             folder.findChildFiles(juce::File::findFilesAndDirectories, false, pattern)) {
            item.deleteRecursively();   // still loaded means it goes next time
        }
    }
}

InstallResult installAll(const juce::File& staged) {
    InstallResult result;

    // A .vst3 goes in the VST3 folder and a .component in Components; pairing
    // them by extension avoids hard-coding which formats a platform ships.
    for (const auto& bundle :
         staged.findChildFiles(juce::File::findDirectories, false, "*")) {
        const auto name = bundle.getFileName();
        if (!isBundleName(name)) continue;

        for (const auto& folder : pluginFolders()) {
            const bool wantsVst3 = name.endsWithIgnoreCase(".vst3") &&
                                   folder.getFileName().equalsIgnoreCase("VST3");
            const bool wantsAu = name.endsWithIgnoreCase(".component") &&
                                 folder.getFileName().equalsIgnoreCase("Components");
            if (!wantsVst3 && !wantsAu) continue;

            folder.createDirectory();
            if (!folder.isDirectory()) continue;

            const auto target = folder.getChildFile(name);
            juce::String error;
            if (replaceBundle(target, bundle, error)) {
                ++result.installed;
                result.paths.add(target.getFullPathName());
            } else if (result.error.isEmpty()) {
                result.error = error;
            }
            break;   // first matching folder wins
        }
    }

    if (result.installed == 0 && result.error.isEmpty()) {
        result.error = "The download did not contain any plugins.";
    }
    return result;
}

int installedVersionCode() {
    const auto json = juce::JSON::parse(supportDir().getChildFile("installed.json"));
    return json.isObject() ? static_cast<int>(json.getProperty("versionCode", 0)) : 0;
}

juce::String installedVersionName() {
    const auto json = juce::JSON::parse(supportDir().getChildFile("installed.json"));
    return json.isObject() ? json.getProperty("versionName", "none").toString() : "none";
}

void recordInstalledVersion(int versionCode, const juce::String& versionName) {
    auto* obj = new juce::DynamicObject();
    obj->setProperty("versionCode", versionCode);
    obj->setProperty("versionName", versionName);
    obj->setProperty("installedAt", juce::Time::getCurrentTime().toISO8601(true));
    supportDir().getChildFile("installed.json").replaceWithText(juce::JSON::toString(juce::var(obj)));
}

Presence whatIsInstalled() {
    Presence p;
    for (const auto& folder : pluginFolders()) {
        if (folder.getChildFile("Harmonizer.vst3").isDirectory()) p.vst3 = true;
        if (folder.getChildFile("Harmonizer.component").isDirectory()) p.audioUnit = true;
    }
    return p;
}

}  // namespace harmonizer::install
