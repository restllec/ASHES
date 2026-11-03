param(
    [string]$Version = "1.8.0",
    [string]$Repo = "restllec/ASHES",
    [string]$OutputDir = "$PSScriptRoot\bin\Release\x86"
)

$filesToInclude = @(
    "AshesAgent.dll",
    "AshesClient.dll",
    "JBKBot.lua",
    "jtu_bot.lua",
    "lesorub.lua",
    "PilotTelemetry.lua",
    "PlowBot.lua",
    "PlowMarks.txt",
    "PolivTram.lua",
    "tobacco.lua",
    "TramBot.lua"
)

$optionalModules = @(
    "plugin.dll",
    "extension.dll"
)

$fileEntries = @()
foreach ($fn in ($filesToInclude + $optionalModules)) {
    $fp = Join-Path $OutputDir $fn
    if (Test-Path $fp) {
        $hash = (Get-FileHash -Path $fp -Algorithm SHA256).Hash.ToLower()
        $url = "https://github.com/$Repo/releases/latest/download/$fn"
        $isOptional = $optionalModules -contains $fn
        $fileEntries += [PSCustomObject]@{
            name = $fn
            url = $url
            sha256 = $hash
            optional = $isOptional
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
