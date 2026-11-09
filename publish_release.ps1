param(
    [string]$Version = "1.8.3",
    [string]$Title = "ASHES v$Version (PolivTram Multi-Route, Tram Distance & GUI Alignment)",
    [string]$Notes = "Release v$($Version) - PolivTram lifecycle & multi-route fixes, TramBot wagon distance control, unified toast notification queue, and Loader/Client UI layout alignment.",
    [switch]$SkipBuild
)

$ErrorActionPreference = "Stop"

$repo = "restllec/ASHES"
$outputDir = "$PSScriptRoot\bin\Release\x86"
$msbuild = "C:\Program Files\Microsoft Visual Studio\18\Insiders\MSBuild\Current\Bin\MSBuild.exe"
$gh = "C:\Program Files\GitHub CLI\gh.exe"
$rar = "C:\Program Files\WinRAR\Rar.exe"
$desktopArchive = "C:\Users\restleSS\Desktop\ASHES.rar"
$projectArchive = "$outputDir\ASHES.rar"

$updaterHeader = Join-Path $PSScriptRoot "Loader\updater.h"
if (Test-Path $updaterHeader) {
    $hdrContent = Get-Content $updaterHeader -Raw
    if ($hdrContent -match 'CURRENT_LOADER_VERSION\s*=\s*"([^"]+)"' -and $Matches[1] -ne $Version) {
        Write-Host "Sync version in updater.h: $($Matches[1]) -> $Version" -ForegroundColor Yellow
        $hdrContent = $hdrContent -replace 'CURRENT_LOADER_VERSION\s*=\s*"[^"]+"', "CURRENT_LOADER_VERSION = `"$Version`""
        Set-Content -Path $updaterHeader -Value $hdrContent -Encoding UTF8
    }
}

if (!$SkipBuild) {
    Write-Host "=== [1/4] Сборка проекта (MSBuild Release x86) ===" -ForegroundColor Cyan
    & $msbuild "$PSScriptRoot\DarkFlame.sln" /p:Configuration=Release /p:Platform=x86 /v:m
} else {
    Write-Host "=== [1/4] Сборка пропущена (SkipBuild) ===" -ForegroundColor Cyan
}

if (Test-Path "$PSScriptRoot\PolivTram.lua") {
    Copy-Item "$PSScriptRoot\PolivTram.lua" "$outputDir\PolivTram.lua" -Force
}
if (Test-Path "$PSScriptRoot\TaxiCatcher.lua") {
    Copy-Item "$PSScriptRoot\TaxiCatcher.lua" "$outputDir\TaxiCatcher.lua" -Force
}

Write-Host "=== [2/4] Генерация манифеста version.json ===" -ForegroundColor Cyan
& "$PSScriptRoot\generate_version_manifest.ps1" -Version $Version -Repo $repo -OutputDir $outputDir

Write-Host "=== [3/4] Упаковка архива ASHES.rar ===" -ForegroundColor Cyan
$filesToPack = @(
    "$outputDir\Ashes.exe",
    "$outputDir\AshesAgent.dll",
    "$outputDir\AshesClient.dll",
    "$outputDir\JBKBot.lua",
    "$outputDir\jtu_bot.lua",
    "$outputDir\lesorub.lua",
    "$outputDir\PilotTelemetry.lua",
    "$outputDir\PlowBot.lua",
    "$outputDir\PlowMarks.txt",
    "$outputDir\PolivTram.lua",
    "$outputDir\TaxiCatcher.lua",
    "$outputDir\tobacco.lua",
    "$outputDir\TramBot.lua"
)

& $rar a -ep -m5 $projectArchive $filesToPack
Copy-Item $projectArchive $desktopArchive -Force
Write-Host "Архив обновлен: $desktopArchive" -ForegroundColor Green

Write-Host "=== [4/4] Публикация релиза на GitHub ($repo) ===" -ForegroundColor Cyan
$assets = @(
    "$outputDir\version.json",
    "$outputDir\Ashes.exe",
    "$outputDir\AshesAgent.dll",
    "$outputDir\AshesClient.dll",
    "$outputDir\TramBot.lua",
    "$outputDir\PlowBot.lua",
    "$outputDir\PolivTram.lua",
    "$outputDir\JBKBot.lua",
    "$outputDir\jtu_bot.lua",
    "$outputDir\lesorub.lua",
    "$outputDir\TaxiCatcher.lua",
    "$outputDir\tobacco.lua",
    "$outputDir\PilotTelemetry.lua",
    "$outputDir\PlowMarks.txt",
    $desktopArchive
)

$existingReleases = & $gh release list --repo $repo
$releaseExists = $existingReleases | Where-Object { $_ -match "\bv$([regex]::Escape($Version))\b" }
if ($releaseExists) {
    Write-Host "Updating release assets on GitHub (v$Version)..." -ForegroundColor Yellow
    & $gh release upload "v$Version" $assets --repo $repo --clobber
    Write-Host "Release assets updated on GitHub!" -ForegroundColor Green
} else {
    Write-Host "Creating new release on GitHub (v$Version)..." -ForegroundColor Yellow
    & $gh release create "v$Version" $assets --repo $repo --title "$Title" --notes "$Notes" --target main
    Write-Host "Release created on GitHub!" -ForegroundColor Green
}
