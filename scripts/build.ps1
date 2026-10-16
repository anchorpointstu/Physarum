<#
.SYNOPSIS
    Builds Physarum (After Effects plugin + CLI test harness) with MSVC and CMake.

.EXAMPLE
    scripts\build.ps1                  # Release
    scripts\build.ps1 -Config All      # Release and Debug
    scripts\build.ps1 -Clean -Test     # clean rebuild, then run the CLI self-checks
#>
[CmdletBinding()]
param(
    [ValidateSet('Release', 'Debug', 'All')]
    [string]$Config = 'Release',
    [string]$SdkRoot,
    [switch]$Clean,
    [switch]$Test
)

$ErrorActionPreference = 'Stop'
$repo = Split-Path -Parent $PSScriptRoot

# --- Locate Visual Studio (Build Tools are enough) ------------------------------
# The Visual Studio (MSBuild) generator is used rather than Ninja. Ninja runs link
# steps through "cmd /C", and a Command Processor AutoRun registry entry (such as
# "cd /d C:\xampp\htdocs") changes their working directory and breaks them.
# MSBuild starts the compiler and linker directly.
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
if (-not (Test-Path $vswhere)) { throw "vswhere.exe not found. Install Visual Studio 2022 Build Tools with the C++ workload." }
$vsRoot = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (-not $vsRoot) { throw "No Visual Studio installation with the x64 C++ tools was found." }

$vcvars = Join-Path $vsRoot 'VC\Auxiliary\Build\vcvars64.bat'
$cmake = Join-Path $vsRoot 'Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe'
foreach ($p in $vcvars, $cmake) {
    if (-not (Test-Path $p)) { throw "Missing build tool: $p" }
}

# --- Locate the After Effects SDK ------------------------------------------------
if (-not $SdkRoot) { $SdkRoot = $env:AE_SDK_ROOT }
if (-not $SdkRoot) { $SdkRoot = 'C:\dev\prereq\AfterEffectsSDK_26.5_win\AfterEffectsSDK_26.5_win' }
$buildPlugin = 'ON'
if (-not (Test-Path (Join-Path $SdkRoot 'Examples\Headers\AE_Effect.h'))) {
    Write-Warning "After Effects SDK not found at '$SdkRoot'. Building the CLI only. Pass -SdkRoot or set AE_SDK_ROOT."
    $buildPlugin = 'OFF'
}

$configs = if ($Config -eq 'All') { @('Release', 'Debug') } else { @($Config) }

foreach ($cfg in $configs) {
    $bdir = Join-Path $repo "build\$cfg"
    if ($Clean -and (Test-Path $bdir)) { Remove-Item -Recurse -Force $bdir }
    New-Item -ItemType Directory -Force $bdir | Out-Null

    $aex = Join-Path $bdir 'Physarum.aex'
    $exportsTxt = Join-Path $bdir 'exports.txt'
    $log = Join-Path $bdir 'build.log'

    # A generated .cmd avoids PowerShell 5.1's fragile quoting of nested arguments to cmd.exe.
    $script = Join-Path $bdir '_build.cmd'
    @(
        '@echo off'
        "cd /d `"$repo`""
        "set `"PATH=$(Split-Path -Parent $vswhere);%PATH%`""
        "call `"$vcvars`" >nul || exit /b 1"
        "`"$cmake`" -S `"$repo`" -B `"$bdir`" -G `"Visual Studio 17 2022`" -A x64 `"-DAE_SDK_ROOT=$SdkRoot`" -DPHYSARUM_BUILD_PLUGIN=$buildPlugin || exit /b 1"
        "`"$cmake`" --build `"$bdir`" --config $cfg -- /m /nologo /v:minimal || exit /b 1"
        "if exist `"$aex`" dumpbin /nologo /exports `"$aex`" > `"$exportsTxt`""
        'exit /b 0'
    ) | Set-Content -Encoding ascii $script

    Write-Host "=== Building $cfg ===" -ForegroundColor Cyan
    # Native tools write progress to stderr; don't let PowerShell 5.1 turn that into an exception.
    $prevPref = $ErrorActionPreference
    $ErrorActionPreference = 'Continue'
    & cmd.exe /d /c $script 2>&1 | ForEach-Object { "$_" } | Tee-Object -FilePath $log
    $exit = $LASTEXITCODE
    $ErrorActionPreference = $prevPref
    if ($exit -ne 0) { throw "Build failed ($cfg). Full log: $log" }

    if (Test-Path $aex) {
        $exports = Get-Content $exportsTxt -Raw
        foreach ($sym in 'EffectMain', 'PluginDataEntryFunction2') {
            if ($exports -notmatch "\b$sym\b") { throw "$aex does not export $sym." }
        }
        Write-Host "Plugin: $aex" -ForegroundColor Green
    }
    Write-Host "CLI:    $(Join-Path $bdir 'physarum_cli.exe')" -ForegroundColor Green

    if ($Test) {
        $cli = Join-Path $bdir 'physarum_cli.exe'
        Write-Host "=== Self-checks ($cfg) ===" -ForegroundColor Cyan
        & $cli --width 512 --height 512 --frames 40 --selfcheck --no-images
        if ($LASTEXITCODE -ne 0) { throw "Determinism self-check failed ($cfg)." }
        & $cli --width 512 --height 512 --frames 50 --snapshot-check 12 --no-images
        if ($LASTEXITCODE -ne 0) { throw "Snapshot self-check failed ($cfg)." }
    }
}
