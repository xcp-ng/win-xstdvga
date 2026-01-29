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
    [string]$Platform
)

# Drivers are ordered by build date first so Hmm gives you a more granular revision number (down to the minute).
$Epoch = [datetime]::new(2026, 1, 1, 0, 0, 0, [System.DateTimeKind]::Utc)
$DriverDate = ([datetime]::UtcNow - $Epoch).Days
$DriverTime = Get-Date -Format Hmm

$ErrorActionPreference = "Stop"

$BuildArgs = @(
    (Resolve-Path "xstdvga.sln"),
    "/m:4",
    "/p:Configuration=$Configuration",
    "/p:Platform=$Platform",
    "/p:XcpngVersionBuild=$DriverDate",
    "/p:XcpngVersionRevision=$DriverTime",
    "/t:$Target"
)

msbuild.exe @BuildArgs
if ($LASTEXITCODE -ne 0) {
    throw "MSBuild failed with error $LASTEXITCODE"
}
