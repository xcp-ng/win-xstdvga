# SPDX-License-Identifier: BSD-2-Clause

[CmdletBinding()]
param (
    [Parameter(Mandatory)]
    [ValidateSet("Debug", "Release")]
    [string]$Configuration,
    [Parameter(Mandatory)]
    [ValidateSet("x64", "arm64")]
    [string]$Platform,
    [Parameter()]
    [string]$Project = "xstdvga",
    [Parameter()]
    [ValidateSet("vs2022")]
    [string]$SolutionDir = "vs2022",
    [Parameter()]
    [string]$CodeQL = "codeql",
    [Parameter()]
    [string]$DVL
)

$ErrorActionPreference = "Stop"

if (!$DVL) {
    $WDKContentRoot = (msbuild -getProperty:WDKContentRoot (Resolve-Path "$SolutionDir\$Project.vcxproj"))
    if ($LASTEXITCODE -ne 0) {
        throw "msbuild cannot find WDKContentRoot with error $LASTEXITCODE"
    }
    $DVL = (Join-Path $WDKContentRoot "Tools\dvl\dvl.exe")
    Write-Verbose "Using detected DVL at $DVL"
}

Push-Location $PSScriptRoot
try {
    Remove-Item -Force -Recurse $SolutionDir\$Platform\$Configuration -ErrorAction SilentlyContinue
    Remove-Item -Force -Recurse database -ErrorAction SilentlyContinue
    & $CodeQL database create database --language=cpp --source-root=. --command="powershell.exe -file .\build.ps1 -Configuration $Configuration -Platform $Platform -Project $Project -SolutionDir $SolutionDir -CodeAnalysis -SignMode Off"
    if ($LASTEXITCODE -ne 0) {
        throw "CodeQL failed with error $LASTEXITCODE"
    }
    & $CodeQL database analyze database microsoft/windows-drivers@1.8.2:windows-driver-suites/recommended.qls --format=sarifv2.1.0 --output=$SolutionDir\$Platform\$Configuration\$Project.sarif
    if ($LASTEXITCODE -ne 0) {
        throw "CodeQL failed with error $LASTEXITCODE"
    }
    Push-Location $SolutionDir\$Platform\$Configuration
    try {
        & $DVL /manualCreate $Project $Platform /sarifPath .
        if ($LASTEXITCODE -ne 0) {
            throw "dvl.exe failed with error $LASTEXITCODE"
        }
    }
    finally {
        Pop-Location
    }
}
finally {
    Pop-Location
}
