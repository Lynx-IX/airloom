# Builds Airloom, runs its self-test and makes what goes on a releases page:
#   Release\Airloom-Setup-<version>.exe    the installer (needs Inno Setup 6)
#   Release\Airloom for Windows.zip        the portable copy: Airloom.exe + How to install.txt
#
# Run from the project folder:  powershell -ExecutionPolicy Bypass -File package.ps1
#   -Build <folder>   where to build (default %TEMP%\LiveRig-build: MSBuild breaks past 260 characters)
#   -SkipTests        don't run the self-test
#   -Sign <command>   sign the program and installer, e.g. "signtool sign /fd sha256 /tr http://timestamp.digicert.com /td sha256 /a"

param(
    [string] $Build = (Join-Path $env:TEMP "LiveRig-build"),
    [switch] $SkipTests,
    [string] $Sign = ""
)

$ErrorActionPreference = "Stop"
$project = $PSScriptRoot

if (Get-Process "Airloom" -ErrorAction SilentlyContinue | Where-Object { $_.Path -like "$Build*" }) {
    throw "Close Airloom first - Windows won't let the build replace a running program."
}

# The version comes from CMakeLists.txt: project(LiveRig VERSION x.y.z ...)
$version = [regex]::Match((Get-Content (Join-Path $project "CMakeLists.txt") -Raw), "project\(LiveRig VERSION ([0-9.]+)").Groups[1].Value
Write-Host "Airloom $version"

# JUCE writes the version into the exe's details only when this file is missing.
Remove-Item (Join-Path $build "LiveRig_artefacts\JuceLibraryCode\LiveRig_resources.rc") -ErrorAction SilentlyContinue
cmake -S $project -B $build -A x64 | Out-Null
if ($LASTEXITCODE -ne 0) { throw "CMake couldn't set up the build." }
cmake --build $build --config Release --parallel
if ($LASTEXITCODE -ne 0) { throw "The build failed." }
$exe = Join-Path $build "LiveRig_artefacts\Release\Airloom.exe"

if (-not $SkipTests) {
    $report = Join-Path $build "selftest.txt"
    Remove-Item $report -ErrorAction SilentlyContinue
    Start-Process $exe -ArgumentList "--fx-selftest `"$report`"" -Wait
    $text = Get-Content $report -ErrorAction SilentlyContinue
    if (-not $text -or -not ($text | Select-String "ALL PASSED")) {
        $text | Select-String "FAIL" | ForEach-Object { Write-Host $_ }
        throw "The self-test didn't pass (report: $report)."
    }
    Write-Host "Self-test passed"
}

$out = Join-Path $project "Release"
$folder = Join-Path $out "Airloom"
$zip = Join-Path $out "Airloom for Windows.zip"
Remove-Item $out -Recurse -Force -ErrorAction SilentlyContinue
New-Item -ItemType Directory -Force $folder | Out-Null
Copy-Item $exe $folder
Copy-Item (Join-Path $project "Packaging\How to install.txt") $folder
Copy-Item (Join-Path $project "LICENSE") (Join-Path $folder "LICENSE.txt")

if ($Sign) {
    Invoke-Expression "$Sign `"$(Join-Path $folder 'Airloom.exe')`""
    if ($LASTEXITCODE -ne 0) { throw "Signing Airloom.exe failed." }
}
Compress-Archive -Path $folder -DestinationPath $zip
Write-Host ("Portable: $zip ({0:N1} MB)" -f ((Get-Item $zip).Length / 1MB))

$iscc = @((Get-Command iscc -ErrorAction SilentlyContinue).Source,
          "${env:ProgramFiles(x86)}\Inno Setup 6\ISCC.exe",
          "$env:ProgramFiles\Inno Setup 6\ISCC.exe",
          "$env:LOCALAPPDATA\Programs\Inno Setup 6\ISCC.exe") | Where-Object { $_ -and (Test-Path $_) } | Select-Object -First 1
if (-not $iscc) {
    Write-Host "Inno Setup 6 isn't installed, so no installer (winget install JRSoftware.InnoSetup)."
    return
}

# The exe in the folder is already signed when signing, so the installer only signs itself.
$isccArgs = @("/Q", "/DAppVersion=$version", "/DExe=$(Join-Path $folder 'Airloom.exe')")
if ($Sign) { $isccArgs += @("/DSign", "/Sairloom=$Sign `$f") }
& $iscc @isccArgs (Join-Path $project "Packaging\Airloom.iss")
if ($LASTEXITCODE -ne 0) { throw "The installer didn't build." }
Write-Host "Installer: $(Join-Path $out "Airloom-Setup-$version.exe")"
