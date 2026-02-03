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
    [switch]$NoDate,
    [Parameter()]
    [string]$Project = "xstdvga",
    [Parameter()]
    [ValidateSet("vs2022")]
    [string]$SolutionDir = "vs2022",
    [Parameter()]
    [string]$SignMode = "TestSign"
)

$ErrorActionPreference = "Stop"

$BuildArgs = @(
    (Resolve-Path "$SolutionDir\$Project.sln"),
    "/t:$Target",
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

if (!$NoDate) {
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
