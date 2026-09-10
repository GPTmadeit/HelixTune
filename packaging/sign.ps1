<#
.SYNOPSIS
    Authenticode-signs the HELIX Tune binaries and installer.

.DESCRIPTION
    Signs in the order that matters: the VST3 and the standalone app first, then
    the installer that carries them. Signing the installer alone leaves the
    files it drops on disk unsigned, and those are the ones a DAW loads.

    Supports the three ways a Windows code-signing key can be held today:

      -Thumbprint    a certificate in the Windows store. This is how hardware
                     tokens work (Sectigo, DigiCert, SSL.com) - the key never
                     leaves the device and the CSP prompts for a PIN.
      -PfxPath       a .pfx file. Only legal for internal or test certificates;
                     publicly-trusted keys have not been allowed in software
                     since June 2023.
      -AzureMetadata Azure Trusted Signing, where the key lives in Microsoft's
                     HSM and signing happens over the wire.

.EXAMPLE
    .\sign.ps1 -Thumbprint A1B2C3... -Artefacts "C:\build\HelixTune\HelixTune_artefacts\Release"

.EXAMPLE
    .\sign.ps1 -PfxPath cert.pfx -PfxPassword (Read-Host -AsSecureString) -Artefacts ...
#>

[CmdletBinding(DefaultParameterSetName = 'Thumbprint')]
param(
    [Parameter(ParameterSetName = 'Thumbprint', Mandatory)]
    [string] $Thumbprint,

    [Parameter(ParameterSetName = 'Pfx', Mandatory)]
    [string] $PfxPath,

    [Parameter(ParameterSetName = 'Pfx')]
    [System.Security.SecureString] $PfxPassword,

    [Parameter(ParameterSetName = 'Azure', Mandatory)]
    [string] $AzureMetadata,

    [Parameter(ParameterSetName = 'Azure')]
    [string] $AzureDlib = "$env:USERPROFILE\.nuget\packages\microsoft.trusted.signing.client\1.0.53\bin\x64\Azure.CodeSigning.Dlib.dll",

    [string] $Artefacts = "$env:USERPROFILE\build\HelixTune\HelixTune_artefacts\Release",

    [string] $Installer,

    # A signature outlives the certificate only if it is timestamped. Without
    # this, everything you shipped stops validating the day the cert expires.
    [string] $TimestampUrl = 'http://timestamp.digicert.com',

    [switch] $WhatIfOnly
)

$ErrorActionPreference = 'Stop'

function Find-SignTool {
    $candidates = Get-ChildItem "${env:ProgramFiles(x86)}\Windows Kits\10\bin" -Recurse -Filter signtool.exe -ErrorAction SilentlyContinue |
                  Where-Object { $_.FullName -match '\\x64\\' } |
                  Sort-Object FullName -Descending

    if (-not $candidates) {
        throw "signtool.exe not found. Install the Windows SDK (it ships with the Visual Studio C++ workload)."
    }

    return $candidates[0].FullName
}

$signtool = Find-SignTool
Write-Host "signtool : $signtool"

# Build the credential half of the command line once.
switch ($PSCmdlet.ParameterSetName) {
    'Thumbprint' {
        $credential = @('/sha1', $Thumbprint)
        Write-Host "identity : store certificate $Thumbprint"
    }
    'Pfx' {
        if (-not (Test-Path $PfxPath)) { throw "PFX not found: $PfxPath" }
        $credential = @('/f', $PfxPath)
        if ($PfxPassword) {
            $plain = [Runtime.InteropServices.Marshal]::PtrToStringAuto(
                        [Runtime.InteropServices.Marshal]::SecureStringToBSTR($PfxPassword))
            $credential += @('/p', $plain)
        }
        Write-Host "identity : PFX $PfxPath"
    }
    'Azure' {
        if (-not (Test-Path $AzureDlib)) { throw "Trusted Signing dlib not found: $AzureDlib" }
        $credential = @('/dlib', $AzureDlib, '/dmdf', $AzureMetadata)
        Write-Host "identity : Azure Trusted Signing"
    }
}

$targets = @()

# The plugin binary and the standalone app, inside the build tree.
$vst3 = Join-Path $Artefacts 'VST3\HELIX Tune.vst3\Contents\x86_64-win\HELIX Tune.vst3'
$app  = Join-Path $Artefacts 'Standalone\HELIX Tune.exe'

foreach ($f in @($vst3, $app)) {
    if (Test-Path $f) { $targets += $f } else { Write-Warning "missing, skipping: $f" }
}

if ($Installer) {
    if (Test-Path $Installer) { $targets += $Installer }
    else { Write-Warning "missing, skipping: $Installer" }
}

if (-not $targets) { throw "Nothing to sign." }

foreach ($t in $targets) {
    Write-Host ""
    Write-Host "signing  : $t"

    $args = @('sign') + $credential + @(
        '/fd', 'SHA256',           # file digest
        '/td', 'SHA256',           # timestamp digest
        '/tr', $TimestampUrl,      # RFC3161 timestamp server
        '/d',  'HELIX Tune',
        '/du', 'https://github.com/GPTmadeit/HelixTune',
        '/v', $t)

    if ($WhatIfOnly) {
        Write-Host "  would run: signtool $($args -join ' ')"
        continue
    }

    & $signtool @args
    if ($LASTEXITCODE -ne 0) { throw "signtool failed on $t (exit $LASTEXITCODE)" }
}

if (-not $WhatIfOnly) {
    Write-Host ""
    Write-Host "verifying..."
    foreach ($t in $targets) {
        $sig = Get-AuthenticodeSignature $t
        "{0,-12} {1}" -f $sig.Status, (Split-Path $t -Leaf)
    }

    Write-Host ""
    Write-Host "Status 'Valid' means the chain is trusted on this machine."
    Write-Host "'UnknownError' with a self-signed certificate is expected: the"
    Write-Host "signature is well-formed but its root is not trusted."
}
