#pragma once

#include <juce_core/juce_core.h>
#include <juce_events/juce_events.h>
#include <juce_data_structures/juce_data_structures.h>   // PropertiesFile
#include <atomic>

namespace helix
{

/** Checks the GitHub releases feed for a newer build, and can fetch and launch
    the installer.

    This makes a network request, so it is a setting the user can see and turn
    off, and it is stored outside the plugin state - an update preference
    belongs to the machine, not to a project file. Every failure path is silent:
    a corrector that pops errors because a laptop is offline is worse than one
    that never mentions updates at all.
*/
class UpdateChecker : private juce::Thread
{
public:
    UpdateChecker();
    ~UpdateChecker() override;

    struct Status
    {
        bool checking = false;
        bool checked = false;
        bool updateAvailable = false;
        juce::String latestVersion;
        juce::String releaseUrl;
        juce::String installerUrl;
    };

    /** Starts a check unless one ran recently or checks are disabled. */
    void checkInBackground (bool force = false);

    Status getStatus() const;

    bool areChecksEnabled() const;
    void setChecksEnabled (bool shouldBeEnabled);

    /** Downloads the installer and launches it. Progress is 0..1, or negative
        on failure. */
    void downloadAndLaunchInstaller();

    float getDownloadProgress() const noexcept { return downloadProgress.load(); }
    bool  isDownloading() const noexcept { return downloading.load(); }

    /** Compares dotted numeric versions, tolerating a leading "v". */
    static bool isNewerVersion (const juce::String& candidate, const juce::String& current);

    static juce::String getCurrentVersion();
    static juce::String getReleasesPageUrl();

private:
    void run() override;
    juce::PropertiesFile& settings() const;

    enum class Job { check, download };
    Job job = Job::check;

    mutable juce::CriticalSection lock;
    Status status;

    std::atomic<float> downloadProgress { 0.0f };
    std::atomic<bool> downloading { false };

    mutable std::unique_ptr<juce::PropertiesFile> props;
};

} // namespace helix
