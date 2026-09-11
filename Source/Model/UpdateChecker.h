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

    It also runs what it downloads, and the installer asks for admin rights, so
    nothing is launched on trust. The URL has to be one of this repository's own
    release downloads, and the file has to match the size and SHA-256 digest
    GitHub publishes for that asset before it is started. A release without a
    digest is offered as a page to open, never as an automatic install.
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

        // Set only for an asset that can be verified end to end.
        juce::String installerUrl;
        juce::int64  installerSize = 0;
        juce::String installerSha256;   // lowercase hex
    };

    /** Starts a check unless one ran recently or checks are disabled. */
    void checkInBackground (bool force = false);

    Status getStatus() const;

    bool areChecksEnabled() const;
    void setChecksEnabled (bool shouldBeEnabled);

    /** Downloads the installer, verifies it, and launches it. Progress is 0..1,
        or negative on failure - including a failed verification. */
    void downloadAndLaunchInstaller();

    float getDownloadProgress() const noexcept { return downloadProgress.load(); }
    bool  isDownloading() const noexcept { return downloading.load(); }

    /** Compares dotted numeric versions, tolerating a leading "v". */
    static bool isNewerVersion (const juce::String& candidate, const juce::String& current);

    /** True only for an https download from this repository's releases of a
        plain file name ending in .exe - no other host, owner or scheme, and no
        query strings, encoded characters or extra path segments. */
    static bool isTrustedInstallerUrl (const juce::String& url);

    /** Parses GitHub's "sha256:<hex>" asset digest. Empty if malformed. */
    static juce::String parseSha256Digest (const juce::String& digest);

    /** True if the file is exactly the expected size and hashes to the
        expected SHA-256. A missing expectation is a failure, not a pass. */
    static bool verifyDownload (const juce::File& file, juce::int64 expectedSize,
                                const juce::String& expectedSha256);

    static juce::String getCurrentVersion();
    static juce::String getReleasesPageUrl();

private:
    void run() override;
    void runCheck();
    void runDownload();
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
