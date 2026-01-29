# SPDX-License-Identifier: BSD-2-Clause

[CmdletBinding()]
param (
    [Parameter(Mandatory)]
    [ValidateSet("Release")]
    [string]$Configuration,
    [Parameter(Mandatory)]
    [ValidateSet("x64", "arm64")]
    [string]$Platform
)

$ErrorActionPreference = "Stop"

Push-Location $PSScriptRoot
try {
    Remove-Item -Force -Recurse database
    codeql database create database --language=cpp --source-root=. --command="powershell.exe -file .\build.ps1 -Configuration $Configuration -Platform $Platform -CodeAnalysis"
    codeql database analyze database microsoft/windows-drivers@1.8.0:windows-driver-suites/recommended.qls --format=sarifv2.1.0 --output=xstdvga\$Platform\$Configuration\xstdvga.sarif
    Push-Location xstdvga\$Platform\$Configuration
    try {
        & "C:\Program Files (x86)\Windows Kits\10\Tools\dvl\dvl.exe" /manualCreate xstdvga $Platform /sarifPath .
    }
    finally {
        Pop-Location
    }
}
finally {
    Pop-Location
}
