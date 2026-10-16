<#
.SYNOPSIS
    Copies Physarum.aex into the shared Adobe MediaCore plug-ins folder, where
    After Effects (all versions) picks it up. Run from an elevated PowerShell on
    the machine that has After Effects, with After Effects closed.

.EXAMPLE
    scripts\install.ps1                         # uses build\Release\Physarum.aex
    scripts\install.ps1 -Source D:\Physarum.aex
    scripts\install.ps1 -Uninstall
#>
[CmdletBinding()]
param(
    [string]$Source = (Join-Path (Split-Path -Parent $PSScriptRoot) 'build\Release\Physarum.aex'),
    [string]$Destination = (Join-Path $env:CommonProgramFiles 'Adobe\Common\Plug-ins\7.0\MediaCore\Physarum'),
    [switch]$Uninstall
)

$ErrorActionPreference = 'Stop'

$principal = New-Object Security.Principal.WindowsPrincipal([Security.Principal.WindowsIdentity]::GetCurrent())
if (-not $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) {
    throw "Run this from an elevated (Administrator) PowerShell: the plug-ins folder is under Program Files."
}

if (Get-Process -Name 'AfterFX' -ErrorAction SilentlyContinue) {
    throw "After Effects is running. Close it first so the plug-in file is not locked."
}

$target = Join-Path $Destination 'Physarum.aex'
if ($Uninstall) {
    if (Test-Path $target) {
        Remove-Item $target
        Write-Host "Removed $target"
    } else {
        Write-Host "Nothing to remove at $target"
    }
    return
}

if (-not (Test-Path $Source)) { throw "Plug-in not found: $Source. Build it first with scripts\build.ps1." }
New-Item -ItemType Directory -Force $Destination | Out-Null
Copy-Item $Source $target -Force
Write-Host "Installed $target"
Write-Host "Start After Effects and find it under Effect > Simulation > Physarum."
