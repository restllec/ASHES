param(
    [string]$Version = "1.0.3",
    [string]$Title = "ASHES Release v$Version",
    [string]$Notes = "Automatic update release v$Version"
)

$ErrorActionPreference = "Stop"

$repo = "restllec/ASHES"
$outputDir = "$PSScriptRoot\bin\Release\x86"
$msbuild = "C:\Program Files\Microsoft Visual Studio\18\Insiders\MSBuild\Current\Bin\MSBuild.exe"
$gh = "C:\Program Files\GitHub CLI\gh.exe"
$rar = "C:\Program Files\WinRAR\Rar.exe"
$desktopArchive = "C:\Users\restleSS\Desktop\ASHES.rar"
$projectArchive = "$outputDir\ASHES.rar"

Write-Host "=== [1/4] Сборка проекта (MSBuild Release x86) ===" -ForegroundColor Cyan
& $msbuild "$PSScriptRoot\DarkFlame.sln" /p:Configuration=Release /p:Platform=x86 /v:m

Write-Host "=== [2/4] Генерация манифеста version.json ===" -ForegroundColor Cyan
& "$PSScriptRoot\generate_version_manifest.ps1" -Version $Version -Repo $repo -OutputDir $outputDir

Write-Host "=== [3/4] Упаковка архива ASHES.rar ===" -ForegroundColor Cyan
$filesToPack = @(
    "$outputDir\Ashes.exe",
    "$outputDir\AshesAgent.dll",
    "$outputDir\AshesClient.dll",
    "$outputDir\JBKBot.lua",
    "$outputDir\jtu_bot.lua",
    "$outputDir\PilotTelemetry.lua",
    "$outputDir\PlowBot.lua",
    "$outputDir\PlowMarks.txt",
    "$outputDir\PolivTram.lua",
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
    "$outputDir\tobacco.lua",
    "$outputDir\PilotTelemetry.lua",
    "$outputDir\PlowMarks.txt",
    $desktopArchive
)

& $gh release create "v$Version" $assets --title "$Title" --notes "$Notes"
Write-Host "Релиз v$Version успешно опубликован на GitHub!" -ForegroundColor Green
