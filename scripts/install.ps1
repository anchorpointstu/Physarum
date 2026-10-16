<#
.SYNOPSIS
    Installs the Physarum After Effects plug-in. By default it downloads the newest
    Physarum.aex from the GitHub releases of anchorpointstu/Physarum and copies it into
    Adobe's shared MediaCore plug-ins folder.

.DESCRIPTION
    Run on the machine that has After Effects, with After Effects closed, from an
    elevated (Administrator) PowerShell. Nothing needs to be downloaded by hand:

        irm https://raw.githubusercontent.com/anchorpointstu/Physarum/main/scripts/install.ps1 | iex

    Or, with the script saved locally (Windows blocks .ps1 files by default, hence Bypass):

        powershell -ExecutionPolicy Bypass -File install.ps1

    Where the plug-in comes from, in order:
      1. -Source <path>
      2. Physarum.aex next to this script
      3. build\Release\Physarum.aex when run from a repo checkout
      4. the newest GitHub release of -Repo (download verified against GitHub's SHA-256)

    The destination is read from the registry value Adobe documents for plug-in
    installers (HKLM\SOFTWARE\Adobe\After Effects\<version>\CommonPluginInstallPath).
    If no After Effects install is registered, it falls back to
    C:\Program Files\Adobe\Common\Plug-ins\7.0\MediaCore.

.EXAMPLE
    powershell -ExecutionPolicy Bypass -File install.ps1
    powershell -ExecutionPolicy Bypass -File install.ps1 -Version v1.0.0-preview.1
    powershell -ExecutionPolicy Bypass -File install.ps1 -Source D:\Physarum.aex
    powershell -ExecutionPolicy Bypass -File install.ps1 -Uninstall
#>
[CmdletBinding()]
param(
    [string]$Source,
    [string]$Version,                         # release tag; default is the newest release
    [string]$Repo = 'anchorpointstu/Physarum',
    [string]$Destination,
    [switch]$Uninstall
)

$ErrorActionPreference = 'Stop'
$ProgressPreference = 'SilentlyContinue' # Windows PowerShell's progress bar slows downloads a lot

# Reads CommonPluginInstallPath from the 64-bit registry view (correct even when this
# runs in 32-bit PowerShell). Returns the newest After Effects version's value, or $null.
function Get-AdobeCommonPluginFolder {
    $base = [Microsoft.Win32.RegistryKey]::OpenBaseKey(
        [Microsoft.Win32.RegistryHive]::LocalMachine, [Microsoft.Win32.RegistryView]::Registry64)
    try {
        $ae = $base.OpenSubKey('SOFTWARE\Adobe\After Effects')
        if (-not $ae) { return $null }
        # Version keys look like "26.0"; newest first.
        $versions = $ae.GetSubKeyNames() | Where-Object { $_ -match '^\d+(\.\d+)?$' } |
            Sort-Object { [double]$_ } -Descending
        foreach ($v in $versions) {
            $key = $ae.OpenSubKey($v)
            $path = if ($key) { $key.GetValue('CommonPluginInstallPath') } else { $null }
            if ($path) { return [pscustomobject]@{ Version = $v; Path = [string]$path } }
        }
        return $null
    } finally {
        $base.Close()
    }
}

# Downloads Physarum.aex from a GitHub release into a temp folder and returns its path.
function Get-ReleasePlugin([string]$repo, [string]$tag) {
    # Older Windows 10 builds default to TLS 1.0/1.1, which GitHub refuses.
    [Net.ServicePointManager]::SecurityProtocol = [Net.ServicePointManager]::SecurityProtocol -bor [Net.SecurityProtocolType]::Tls12
    $api = "https://api.github.com/repos/$repo/releases"
    try {
        if ($tag) {
            $release = Invoke-RestMethod -UseBasicParsing "$api/tags/$tag"
        } else {
            # Newest release first, including pre-releases (which /releases/latest skips).
            $release = Invoke-RestMethod -UseBasicParsing "$api`?per_page=20" |
                Where-Object { -not $_.draft -and ($_.assets | Where-Object name -eq 'Physarum.aex') } |
                Select-Object -First 1
        }
    } catch {
        $response = $_.Exception.Response
        if ($response -and [int]$response.StatusCode -eq 404) {
            if ($tag) { throw "There is no release '$tag' in $repo. See https://github.com/$repo/releases for the available versions." }
            throw "The repository $repo was not found on GitHub."
        }
        throw "Could not reach GitHub to download the plug-in ($($_.Exception.Message)). Check the internet connection, or pass -Source <path to Physarum.aex>."
    }
    if (-not $release) { throw "No release of $repo contains Physarum.aex." }
    $asset = $release.assets | Where-Object name -eq 'Physarum.aex' | Select-Object -First 1
    if (-not $asset) { throw "Release $($release.tag_name) of $repo has no Physarum.aex." }

    $dir = Join-Path ([IO.Path]::GetTempPath()) ("Physarum-" + [guid]::NewGuid().ToString('N'))
    New-Item -ItemType Directory -Force $dir | Out-Null
    $file = Join-Path $dir 'Physarum.aex'
    Write-Host "Downloading Physarum $($release.tag_name) from github.com/$repo ..."
    Invoke-WebRequest -UseBasicParsing $asset.browser_download_url -OutFile $file

    # GitHub publishes a SHA-256 digest for every release asset; refuse a mismatch.
    $digest = [string]$asset.digest
    if ($digest -match '^sha256:([0-9a-fA-F]{64})$') {
        $actual = (Get-FileHash -Algorithm SHA256 $file).Hash
        if ($actual -ne $Matches[1].ToUpperInvariant()) {
            Remove-Item -Recurse -Force $dir
            throw "The downloaded file does not match GitHub's SHA-256 for this release. Nothing was installed."
        }
        Write-Host "Verified SHA-256 $actual"
    } else {
        Write-Warning "GitHub did not provide a SHA-256 digest for this asset; the download was not verified."
    }
    return $file
}

# --- Where to install -------------------------------------------------------------
if (-not $Destination) {
    $found = Get-AdobeCommonPluginFolder
    if ($found) {
        $Destination = Join-Path $found.Path 'Physarum'
        Write-Host "After Effects $($found.Version) found; common plug-ins folder: $($found.Path)"
    } else {
        # ProgramW6432 is always the 64-bit Program Files, even from 32-bit PowerShell.
        $programFiles = if ($env:ProgramW6432) { $env:ProgramW6432 } else { $env:ProgramFiles }
        $Destination = Join-Path $programFiles 'Adobe\Common\Plug-ins\7.0\MediaCore\Physarum'
        Write-Warning "No After Effects installation is registered on this PC. Installing to the default folder anyway."
    }
}
$target = Join-Path $Destination 'Physarum.aex'

# --- Preconditions ----------------------------------------------------------------
if (Get-Process -Name 'AfterFX' -ErrorAction SilentlyContinue) {
    throw "After Effects is running. Close it first so the plug-in file is not locked."
}

# --- Uninstall --------------------------------------------------------------------
if ($Uninstall) {
    try {
        if (Test-Path $target) {
            Remove-Item $target
            if (-not (Get-ChildItem $Destination -Force | Select-Object -First 1)) { Remove-Item $Destination }
            Write-Host "Removed $target"
        } else {
            Write-Host "Nothing to remove at $target"
        }
    } catch [System.UnauthorizedAccessException] {
        throw "Administrator rights are needed to change $Destination. Right-click PowerShell, choose 'Run as administrator', and run this again."
    }
    return
}

# --- What to install --------------------------------------------------------------
$downloaded = $null
if ($Source) {
    if (-not (Test-Path $Source)) { throw "Plug-in not found: $Source" }
} else {
    $local = @()
    if ($PSScriptRoot) { # empty when run as "irm ... | iex"
        $local += Join-Path $PSScriptRoot 'Physarum.aex'
        $local += Join-Path (Split-Path -Parent $PSScriptRoot) 'build\Release\Physarum.aex'
    }
    $Source = $local | Where-Object { Test-Path $_ } | Select-Object -First 1
    if ($Source -and $Version) { $Source = $null } # an explicit -Version means "from GitHub"
    if ($Source) {
        Write-Host "Using local plug-in $Source"
    } else {
        $Source = Get-ReleasePlugin $Repo $Version
        $downloaded = Split-Path -Parent $Source
    }
}

# --- Install ----------------------------------------------------------------------
try {
    New-Item -ItemType Directory -Force $Destination | Out-Null
    Copy-Item $Source $target -Force
} catch [System.UnauthorizedAccessException] {
    throw "Administrator rights are needed to write to $Destination. Right-click PowerShell, choose 'Run as administrator', and run this again."
} finally {
    if ($downloaded) { Remove-Item -Recurse -Force $downloaded -ErrorAction SilentlyContinue }
}
# A file copied from a download or a zip carries a "from the internet" mark; clear it.
Unblock-File $target -ErrorAction SilentlyContinue
Write-Host "Installed $target"
Write-Host "Start After Effects and find it under Effect > Simulation > Physarum."
