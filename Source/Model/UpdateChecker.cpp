#include "UpdateChecker.h"
#include <juce_cryptography/juce_cryptography.h>   // SHA256

namespace helix
{

namespace
{
    constexpr const char* kOwner = "GPTmadeit";
    constexpr const char* kRepo  = "HelixTune";

    // GitHub rejects API requests without one, and rate-limits by IP.
    constexpr const char* kUserAgent = "HelixTune-Updater";

    constexpr int kMinimumHoursBetweenChecks = 6;
}

UpdateChecker::UpdateChecker() : juce::Thread ("Helix update check") {}

UpdateChecker::~UpdateChecker()
{
    stopThread (4000);
}

juce::String UpdateChecker::getCurrentVersion()
{
   #ifdef HELIX_VERSION
    return HELIX_VERSION;
   #else
    return "0.0.0";
   #endif
}

juce::String UpdateChecker::getReleasesPageUrl()
{
    return juce::String ("https://github.com/") + kOwner + "/" + kRepo + "/releases";
}

juce::PropertiesFile& UpdateChecker::settings() const
{
    if (props == nullptr)
    {
        juce::PropertiesFile::Options options;
        options.applicationName = "HELIX Tune";
        options.folderName = "Helix Audio";
        options.filenameSuffix = "settings";
        options.osxLibrarySubFolder = "Application Support";

        props = std::make_unique<juce::PropertiesFile> (options);
    }

    return *props;
}

bool UpdateChecker::areChecksEnabled() const
{
    return settings().getBoolValue ("updateChecks", true);
}

void UpdateChecker::setChecksEnabled (bool shouldBeEnabled)
{
    settings().setValue ("updateChecks", shouldBeEnabled);
    settings().saveIfNeeded();
}

bool UpdateChecker::isNewerVersion (const juce::String& candidate, const juce::String& current)
{
    auto parts = [] (juce::String v)
    {
        v = v.trim();
        if (v.startsWithIgnoreCase ("v"))
            v = v.substring (1);

        juce::StringArray tokens;
        tokens.addTokens (v.upToFirstOccurrenceOf ("-", false, true), ".", "");

        juce::Array<int> numbers;
        for (const auto& t : tokens)
            numbers.add (t.getIntValue());

        while (numbers.size() < 3)
            numbers.add (0);

        return numbers;
    };

    const auto a = parts (candidate);
    const auto b = parts (current);

    for (int i = 0; i < juce::jmax (a.size(), b.size()); ++i)
    {
        const int x = i < a.size() ? a[i] : 0;
        const int y = i < b.size() ? b[i] : 0;

        if (x != y)
            return x > y;
    }

    return false;
}

bool UpdateChecker::isTrustedInstallerUrl (const juce::String& url)
{
    // Compared exactly, case and all: anything that is not GitHub's own form
    // of this repository's download path is refused rather than normalised.
    const auto prefix = juce::String ("https://github.com/") + kOwner + "/" + kRepo
                      + "/releases/download/";

    if (! url.startsWith (prefix))
        return false;

    // What remains must be exactly "<tag>/<file>.exe", each part made only of
    // characters that cannot carry a query, an escape or a path step.
    const auto rest = url.substring (prefix.length());
    const auto tag  = rest.upToFirstOccurrenceOf ("/", false, false);
    const auto file = rest.fromFirstOccurrenceOf ("/", false, false);

    auto plain = [] (const juce::String& s)
    {
        return s.isNotEmpty()
            && s.containsOnly ("ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789._-")
            && ! s.startsWithChar ('.')
            && ! s.contains ("..");
    };

    return plain (tag) && plain (file) && file.endsWithIgnoreCase (".exe");
}

juce::String UpdateChecker::parseSha256Digest (const juce::String& digest)
{
    if (! digest.startsWithIgnoreCase ("sha256:"))
        return {};

    const auto hex = digest.substring (7).trim().toLowerCase();

    return (hex.length() == 64 && hex.containsOnly ("0123456789abcdef")) ? hex : juce::String();
}

bool UpdateChecker::verifyDownload (const juce::File& file, juce::int64 expectedSize,
                                    const juce::String& expectedSha256)
{
    if (expectedSize <= 0 || expectedSha256.length() != 64 || ! file.existsAsFile())
        return false;

    if (file.getSize() != expectedSize)
        return false;

    return juce::SHA256 (file).toHexString().equalsIgnoreCase (expectedSha256);
}

void UpdateChecker::checkInBackground (bool force)
{
    if (isThreadRunning())
        return;

    if (! force && ! areChecksEnabled())
        return;

    if (! force)
    {
        const auto last = settings().getValue ("lastUpdateCheck", "0").getLargeIntValue();
        const auto now = juce::Time::getCurrentTime().toMilliseconds();

        if (now - last < (juce::int64) kMinimumHoursBetweenChecks * 60 * 60 * 1000)
            return;
    }

    {
        const juce::ScopedLock sl (lock);
        status.checking = true;
    }

    job = Job::check;
    startThread();
}

void UpdateChecker::downloadAndLaunchInstaller()
{
    if (isThreadRunning())
        return;

    downloadProgress = 0.0f;
    downloading = true;
    job = Job::download;
    startThread();
}

UpdateChecker::Status UpdateChecker::getStatus() const
{
    const juce::ScopedLock sl (lock);
    return status;
}

void UpdateChecker::run()
{
    if (job == Job::check)
        runCheck();
    else
        runDownload();
}

void UpdateChecker::runCheck()
{
    const juce::String endpoint = juce::String ("https://api.github.com/repos/")
                                + kOwner + "/" + kRepo + "/releases/latest";

    Status result;
    result.checked = true;

    auto options = juce::URL::InputStreamOptions (juce::URL::ParameterHandling::inAddress)
                       .withConnectionTimeoutMs (8000)
                       .withExtraHeaders (juce::String ("User-Agent: ") + kUserAgent
                                          + "\r\nAccept: application/vnd.github+json");

    if (auto stream = juce::URL (endpoint).createInputStream (options))
    {
        const auto body = stream->readEntireStreamAsString();

        if (! threadShouldExit())
        {
            const auto json = juce::JSON::parse (body);

            const auto tag = json.getProperty ("tag_name", "").toString();
            result.latestVersion = tag;

            // Only ever this repository's own page, whatever the feed says.
            const auto page = json.getProperty ("html_url", "").toString();
            result.releaseUrl = page.startsWith (getReleasesPageUrl() + "/") ? page : getReleasesPageUrl();

            if (const auto* assets = json.getProperty ("assets", {}).getArray())
            {
                for (const auto& a : *assets)
                {
                    const auto name = a.getProperty ("name", "").toString();

                    if (! name.endsWithIgnoreCase (".exe"))
                        continue;

                    const auto url  = a.getProperty ("browser_download_url", "").toString();
                    const auto sha  = parseSha256Digest (a.getProperty ("digest", "").toString());
                    const auto size = (juce::int64) a.getProperty ("size", 0);

                    // Offered for automatic install only if it can be checked
                    // end to end; otherwise the menu falls back to the page.
                    if (isTrustedInstallerUrl (url) && sha.isNotEmpty() && size > 0)
                    {
                        result.installerUrl    = url;
                        result.installerSha256 = sha;
                        result.installerSize   = size;
                    }

                    break;
                }
            }

            result.updateAvailable = tag.isNotEmpty()
                                   && isNewerVersion (tag, getCurrentVersion());
        }
    }

    settings().setValue ("lastUpdateCheck",
                         juce::String (juce::Time::getCurrentTime().toMilliseconds()));
    settings().saveIfNeeded();

    const juce::ScopedLock sl (lock);
    result.checking = false;
    status = result;
}

void UpdateChecker::runDownload()
{
    juce::String url, sha;
    juce::int64 expectedSize = 0;

    {
        const juce::ScopedLock sl (lock);
        url = status.installerUrl;
        sha = status.installerSha256;
        expectedSize = status.installerSize;
    }

    auto fail = [this]
    {
        downloadProgress = -1.0f;
        downloading = false;
    };

    // Checked again here rather than trusted from the check, so no path can
    // reach startAsProcess with an unvetted URL.
    if (! isTrustedInstallerUrl (url) || sha.isEmpty() || expectedSize <= 0)
    {
        fail();
        return;
    }

    // A fresh, unpredictable name every time, so nothing can be planted at a
    // known path ahead of the download.
    const auto target = juce::File::getSpecialLocation (juce::File::tempDirectory)
                            .getNonexistentChildFile ("HELIX-Tune-Setup-"
                                                          + juce::String::toHexString (juce::Random::getSystemRandom().nextInt64()),
                                                      ".exe", false);

    auto options = juce::URL::InputStreamOptions (juce::URL::ParameterHandling::inAddress)
                       .withConnectionTimeoutMs (15000)
                       .withExtraHeaders (juce::String ("User-Agent: ") + kUserAgent);

    if (auto stream = juce::URL (url).createInputStream (options))
    {
        {
            juce::FileOutputStream out (target);

            if (out.openedOk())
            {
                juce::int64 written = 0;
                juce::HeapBlock<char> buffer (32768);

                while (! threadShouldExit())
                {
                    const int read = stream->read (buffer, 32768);
                    if (read <= 0)
                        break;

                    // Never write more than GitHub said the file holds. A larger
                    // file cannot match the digest anyway, and this stops a
                    // hostile response from filling the disk.
                    written += read;
                    if (written > expectedSize)
                        break;

                    out.write (buffer, (size_t) read);
                    downloadProgress = juce::jlimit (0.0f, 1.0f, (float) ((double) written / (double) expectedSize));
                }

                out.flush();
            }
        }   // closed before hashing, and before Windows is asked to run it

        if (! threadShouldExit() && verifyDownload (target, expectedSize, sha))
        {
            downloadProgress = 1.0f;
            downloading = false;

            // The installer replaces files the host may still have loaded, so
            // it is started and left to ask the user to close things.
            target.startAsProcess();
            return;
        }
    }

    // Anything that did not verify is removed rather than left for later.
    target.deleteFile();
    fail();
}

} // namespace helix
