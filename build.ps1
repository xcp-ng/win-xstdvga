# SPDX-License-Identifier: BSD-2-Clause

[CmdletBinding()]
param (
    [Parameter()]
    [string]$Target = "Rebuild",
    [Parameter(Mandatory)]
    [ValidateSet("Debug", "Release")]
    [string]$Configuration,
    [Parameter(Mandatory)]
    [ValidateSet("x64", "arm64")]
    [string]$Platform,
    [Parameter()]
    [switch]$CodeAnalysis,
    [Parameter()]
    [string]$Project = "xstdvga",
    [Parameter()]
    [ValidateSet("vs2022", "vs2026")]
    [string]$SolutionDir = "vs2026",
    [Parameter()]
    [string]$SignMode = "TestSign",
    # Use the INF DriverVer format, e.g. "02/04/2026,0.1.34.1215"
    [Parameter()]
    [string]$DriverVer
)

$ErrorActionPreference = "Stop"

$BuildArgs = @(
    (Resolve-Path "$SolutionDir\$Project.sln"),
    "/t:$Target",
    "/restore",
    "/m:4",
    "/p:Configuration=$Configuration",
    "/p:Platform=$Platform",
    "/p:SignMode=$SignMode"
)

if ($CodeAnalysis) {
    $BuildArgs += @(
        "/p:RunCodeAnalysis=true",
        "/p:EnablePREFast=true"
    )
}

if ($DriverVer) {
    $DriverVerParts = $DriverVer.Split(",")

    $DriverDate = $DriverVerParts[0].Trim()
    if ($DriverDate -notmatch "[0-9]{2}/[0-9]{2}/[0-9]{4}") {
        throw "Malformed driver date"
    }
    $Version = [version]::Parse($DriverVerParts[1].Trim())
    $BuildArgs += @(
        "/p:XcpngDriverDate=$DriverDate",
        "/p:XcpngVersionMajor=$($Version.Major)",
        "/p:XcpngVersionMinor=$($Version.Minor)",
        "/p:XcpngVersionBuild=$($Version.Build)",
        "/p:XcpngVersionRevision=$($Version.Revision)"
    )
}
else {
    # Drivers are ordered by build date first so Hmm gives you a more granular revision number (down to the minute).
    $Epoch = [datetime]::new(2026, 1, 1, 0, 0, 0, [System.DateTimeKind]::Utc)
    $Now = [datetime]::UtcNow
    $DriverDate = $Now.ToString("MM/dd/yyyy")
    $DriverBuild = ($Now - $Epoch).Days
    $DriverRevision = $Now.ToString("Hmm")

    $BuildArgs += @(
        "/p:XcpngDriverDate=$DriverDate",
        "/p:XcpngVersionBuild=$DriverBuild",
        "/p:XcpngVersionRevision=$DriverRevision"
    )
}

msbuild.exe @BuildArgs
if ($LASTEXITCODE -ne 0) {
    throw "MSBuild failed with error $LASTEXITCODE"
}
