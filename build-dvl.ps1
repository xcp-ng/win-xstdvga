# SPDX-License-Identifier: BSD-2-Clause

[CmdletBinding()]
param (
    [Parameter(Mandatory)]
    [ValidateSet("Release")]
    [string]$Configuration,
    [Parameter(Mandatory)]
    [ValidateSet("x64", "arm64")]
    [string]$Platform,
    [Parameter()]
    [string]$Project = "xstdvga",
    [Parameter()]
    [ValidateSet("vs2022")]
    [string]$SolutionDir = "vs2022"
)

$ErrorActionPreference = "Stop"

Push-Location $PSScriptRoot
try {
    Remove-Item -Force -Recurse $SolutionDir\$Platform\$Configuration -ErrorAction SilentlyContinue
    Remove-Item -Force -Recurse database -ErrorAction SilentlyContinue
    codeql database create database --language=cpp --source-root=. --command="powershell.exe -file .\build.ps1 -Configuration $Configuration -Platform $Platform -Project $Project -SolutionDir $SolutionDir -CodeAnalysis"
    if ($LASTEXITCODE -ne 0) {
        throw "CodeQL failed with error $LASTEXITCODE"
    }
    codeql database analyze database microsoft/windows-drivers@1.8.0:windows-driver-suites/recommended.qls --format=sarifv2.1.0 --output=$SolutionDir\$Platform\$Configuration\$Project.sarif
    if ($LASTEXITCODE -ne 0) {
        throw "CodeQL failed with error $LASTEXITCODE"
    }
    Push-Location $SolutionDir\$Platform\$Configuration
    try {
        & "C:\Program Files (x86)\Windows Kits\10\Tools\dvl\dvl.exe" /manualCreate $Project $Platform /sarifPath .
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
