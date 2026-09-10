; Inno Setup script for HELIX Tune.
;
; Build with:
;   ISCC.exe /DArtefacts="<path to HelixTune_artefacts\Release>" /DAppVersion=1.0.0 HelixTune.iss
;
; The artefacts path is a parameter because the build tree lives outside the
; repository - see the README on why building into OneDrive is a bad idea.

#ifndef Artefacts
  #define Artefacts "..\..\..\build\HelixTune\HelixTune_artefacts\Release"
#endif

#ifndef AppVersion
  #define AppVersion "1.0.0"
#endif

#define AppName "HELIX Tune"
#define Publisher "Helix Audio"
#define RepoUrl "https://github.com/GPTmadeit/HelixTune"

[Setup]
; A stable AppId is what lets a later installer recognise and upgrade this one
; in place rather than leaving two entries in Add/Remove Programs.
AppId={{8E1C4D9A-3F27-4B6E-9C15-7A2D5E8F1B34}
AppName={#AppName}
AppVersion={#AppVersion}
AppVerName={#AppName} {#AppVersion}
AppPublisher={#Publisher}
AppPublisherURL={#RepoUrl}
AppSupportURL={#RepoUrl}/issues
AppUpdatesURL={#RepoUrl}/releases
VersionInfoVersion={#AppVersion}

DefaultDirName={autopf}\{#AppName}
DefaultGroupName={#AppName}
DisableProgramGroupPage=yes
DisableDirPage=no
LicenseFile=..\LICENSE

OutputDir=..\dist
OutputBaseFilename=HELIX-Tune-{#AppVersion}-Windows
SetupIconFile=..\docs\icon.ico
UninstallDisplayIcon={app}\HELIX Tune.exe
WizardStyle=modern
Compression=lzma2/max
SolidCompression=yes

; The VST3 goes into a 64-bit system location, so elevation is required and
; the installer must not be silently redirected into the 32-bit view.
PrivilegesRequired=admin
ArchitecturesInstallIn64BitMode=x64compatible
ArchitecturesAllowed=x64compatible

; Plugin files are usually locked by whatever DAW is open. Offer to close them
; rather than failing halfway through with a cryptic error.
CloseApplications=yes
CloseApplicationsFilter=*.exe
RestartApplications=no

; Authenticode signing, enabled by passing /DSignToolName=<name> along with the
; matching /S<name>="..." definition:
;
;   ISCC /DSignToolName=helixsign ^
;        /Shelixsign="signtool.exe sign /sha1 <thumb> /fd SHA256 /tr <url> /td SHA256 $f" ^
;        HelixTune.iss
;
; SignedUninstaller matters: without it the uninstaller written to disk is
; unsigned, and that is the one file a user runs months later when Windows has
; forgotten the installer's reputation.
#ifdef SignToolName
SignTool={#SignToolName}
SignedUninstaller=yes
#endif

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"

[Types]
Name: "full";   Description: "Full installation"
Name: "custom"; Description: "Custom installation"; Flags: iscustom

[Components]
Name: "vst3";       Description: "VST3 plugin";           Types: full; Flags: fixed
Name: "standalone"; Description: "Standalone application"; Types: full

[Files]
Source: "{#Artefacts}\VST3\HELIX Tune.vst3\*"; \
    DestDir: "{commoncf64}\VST3\HELIX Tune.vst3"; \
    Components: vst3; \
    Flags: ignoreversion recursesubdirs createallsubdirs

Source: "{#Artefacts}\Standalone\HELIX Tune.exe"; \
    DestDir: "{app}"; \
    Components: standalone; \
    Flags: ignoreversion

Source: "..\README.md"; DestDir: "{app}"; Flags: ignoreversion
Source: "..\LICENSE";   DestDir: "{app}"; Flags: ignoreversion

[Icons]
Name: "{group}\{#AppName}";           Filename: "{app}\HELIX Tune.exe"; Components: standalone
Name: "{group}\Release notes";        Filename: "{#RepoUrl}/releases"
Name: "{group}\Uninstall {#AppName}"; Filename: "{uninstallexe}"
Name: "{autodesktop}\{#AppName}";     Filename: "{app}\HELIX Tune.exe"; \
    Components: standalone; Tasks: desktopicon

[Tasks]
Name: "desktopicon"; Description: "Create a desktop shortcut"; \
    GroupDescription: "Shortcuts:"; Components: standalone; Flags: unchecked

[Run]
Filename: "{app}\HELIX Tune.exe"; Description: "Launch {#AppName}"; \
    Components: standalone; Flags: nowait postinstall skipifsilent

[UninstallDelete]
Type: filesandordirs; Name: "{commoncf64}\VST3\HELIX Tune.vst3"
