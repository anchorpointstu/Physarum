<#
.SYNOPSIS
    Installs Physarum.aex into Adobe's shared MediaCore plug-ins folder, where
    After Effects picks it up.

.DESCRIPTION
    Run on the machine that has After Effects, with After Effects closed, from an
    elevated (Administrator) PowerShell. On a fresh Windows install scripts are
    blocked by default, so start it like this:

        powershell -ExecutionPolicy Bypass -File install.ps1

    The destination is read from the registry value Adobe documents for plug-in
    installers (HKLM\SOFTWARE\Adobe\After Effects\<version>\CommonPluginInstallPath).
    If no After Effects install is registered, it falls back to
    C:\Program Files\Adobe\Common\Plug-ins\7.0\MediaCore.

.EXAMPLE
    powershell -ExecutionPolicy Bypass -File install.ps1                      # Physarum.aex next to this script
    powershell -ExecutionPolicy Bypass -File install.ps1 -Source D:\Physarum.aex
    powershell -ExecutionPolicy Bypass -File install.ps1 -Uninstall
#>
[CmdletBinding()]
param(
    [string]$Source,
    [string]$Destination,
    [switch]$Uninstall
)

$ErrorActionPreference = 'Stop'

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

# --- What to install --------------------------------------------------------------
if (-not $Uninstall -and -not $Source) {
    $candidates = @(
        (Join-Path $PSScriptRoot 'Physarum.aex'),                                        # shipped next to the script
        (Join-Path (Split-Path -Parent $PSScriptRoot) 'build\Release\Physarum.aex')      # repo build output
    )
    $Source = $candidates | Where-Object { Test-Path $_ } | Select-Object -First 1
    if (-not $Source) {
        throw "Physarum.aex not found. Put it next to install.ps1 or pass -Source <path to Physarum.aex>."
    }
}
if (-not $Uninstall -and -not (Test-Path $Source)) { throw "Plug-in not found: $Source" }

# --- Preconditions ----------------------------------------------------------------
if (Get-Process -Name 'AfterFX' -ErrorAction SilentlyContinue) {
    throw "After Effects is running. Close it first so the plug-in file is not locked."
}

# --- Do it ------------------------------------------------------------------------
try {
    if ($Uninstall) {
        if (Test-Path $target) {
            Remove-Item $target
            if (-not (Get-ChildItem $Destination -Force | Select-Object -First 1)) { Remove-Item $Destination }
            Write-Host "Removed $target"
        } else {
            Write-Host "Nothing to remove at $target"
        }
        return
    }

    New-Item -ItemType Directory -Force $Destination | Out-Null
    Copy-Item $Source $target -Force
} catch [System.UnauthorizedAccessException] {
    throw "Administrator rights are needed to write to $Destination. Right-click PowerShell, choose 'Run as administrator', and run this again."
}
# A file copied from a download or a zip carries a "from the internet" mark; clear it.
Unblock-File $target -ErrorAction SilentlyContinue
Write-Host "Installed $target"
Write-Host "Start After Effects and find it under Effect > Simulation > Physarum."
