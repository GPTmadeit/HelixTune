#include "UpdateChecker.h"

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
                result.releaseUrl = json.getProperty ("html_url", getReleasesPageUrl()).toString();

                // Prefer the Windows installer asset if the release has one.
                if (const auto* assets = json.getProperty ("assets", {}).getArray())
                {
                    for (const auto& a : *assets)
                    {
                        const auto name = a.getProperty ("name", "").toString();

                        if (name.endsWithIgnoreCase (".exe"))
                        {
                            result.installerUrl = a.getProperty ("browser_download_url", "").toString();
                            break;
                        }
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
        return;
    }

    // ---- download -------------------------------------------------------
    juce::String url;
    {
        const juce::ScopedLock sl (lock);
        url = status.installerUrl;
    }

    if (url.isEmpty())
    {
        downloadProgress = -1.0f;
        downloading = false;
        return;
    }

    const auto target = juce::File::getSpecialLocation (juce::File::tempDirectory)
                            .getChildFile ("HELIX-Tune-Setup.exe");

    target.deleteFile();

    auto options = juce::URL::InputStreamOptions (juce::URL::ParameterHandling::inAddress)
                       .withConnectionTimeoutMs (15000)
                       .withExtraHeaders (juce::String ("User-Agent: ") + kUserAgent);

    if (auto stream = juce::URL (url).createInputStream (options))
    {
        juce::FileOutputStream out (target);

        if (out.openedOk())
        {
            const auto total = stream->getTotalLength();
            juce::int64 written = 0;
            juce::HeapBlock<char> buffer (32768);

            while (! threadShouldExit())
            {
                const int read = stream->read (buffer, 32768);
                if (read <= 0)
                    break;

                out.write (buffer, (size_t) read);
                written += read;

                if (total > 0)
                    downloadProgress = juce::jlimit (0.0f, 1.0f, (float) ((double) written / (double) total));
            }

            out.flush();

            if (! threadShouldExit() && written > 0)
            {
                downloadProgress = 1.0f;
                downloading = false;

                // The installer replaces files the host may still have loaded,
                // so it is started and left to ask the user to close things.
                target.startAsProcess();
                return;
            }
        }
    }

    downloadProgress = -1.0f;
    downloading = false;
}

} // namespace helix
