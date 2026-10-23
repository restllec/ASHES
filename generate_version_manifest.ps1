param(
    [string]$Version = "1.0.3",
    [string]$Repo = "restllec/ASHES",
    [string]$OutputDir = "$PSScriptRoot\bin\Release\x86"
)

$filesToInclude = @(
    "AshesAgent.dll",
    "AshesClient.dll",
    "JBKBot.lua",
    "jtu_bot.lua",
    "PilotTelemetry.lua",
    "PlowBot.lua",
    "PlowMarks.txt",
    "PolivTram.lua",
    "tobacco.lua",
    "TramBot.lua"
)

$fileEntries = @()
foreach ($fn in $filesToInclude) {
    $fp = Join-Path $OutputDir $fn
    if (Test-Path $fp) {
        $hash = (Get-FileHash -Path $fp -Algorithm SHA256).Hash.ToLower()
        $url = "https://github.com/$Repo/releases/latest/download/$fn"
        $fileEntries += [PSCustomObject]@{
            name = $fn
            url = $url
            sha256 = $hash
        }
    }
}

$manifest = [PSCustomObject]@{
    loader = [PSCustomObject]@{
        version = $Version
        url = "https://github.com/$Repo/releases/latest/download/Ashes.exe"
    }
    files = $fileEntries
}

$jsonPath = Join-Path $OutputDir "version.json"
$manifest | ConvertTo-Json -Depth 5 | Set-Content -Path $jsonPath -Encoding UTF8
Write-Host "version.json успешно сгенерирован: $jsonPath"
