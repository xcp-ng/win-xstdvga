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
    [string]$Project = "xstdvga"
)

$ErrorActionPreference = "Stop"

Push-Location $PSScriptRoot
try {
    Remove-Item -Force -Recurse $Project\$Platform\$Configuration -ErrorAction SilentlyContinue
    Remove-Item -Force -Recurse database -ErrorAction SilentlyContinue
    codeql database create database --language=cpp --source-root=. --command="powershell.exe -file .\build.ps1 -Configuration $Configuration -Platform $Platform -CodeAnalysis"
    codeql database analyze database microsoft/windows-drivers@1.8.0:windows-driver-suites/recommended.qls --format=sarifv2.1.0 --output=$Project\$Platform\$Configuration\$Project.sarif
    Push-Location $Project\$Platform\$Configuration
    try {
        & "C:\Program Files (x86)\Windows Kits\10\Tools\dvl\dvl.exe" /manualCreate $Project $Platform /sarifPath .
    }
    finally {
        Pop-Location
    }
}
finally {
    Pop-Location
}
