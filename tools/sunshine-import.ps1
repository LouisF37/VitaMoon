<#
.SYNOPSIS
  Adds your Steam games (and optionally your PS1 games for DuckStation) to
  Sunshine, with their covers, so they appear in the Moonlight Vita library.

.DESCRIPTION
  - Reads the Steam libraries (libraryfolders.vdf + appmanifest_*.acf).
  - For each installed game, adds a Sunshine app that launches it with
    steam://rungameid/<id> (Sunshine's documented method for Steam games).
  - Converts the game's Steam cover (library_600x900) to PNG, the only format
    Sunshine accepts, and points the app to it.
  - Optionally adds one app per PS1 game file for DuckStation
    (-batch -fullscreen, as in DuckStation's command-line documentation).
  - Never removes or changes existing apps; a backup of apps.json is made.

  Run it in an administrator PowerShell (Sunshine's folder is in Program Files):
    powershell -ExecutionPolicy Bypass -File .\sunshine-import.ps1
  Preview without writing anything:
    powershell -ExecutionPolicy Bypass -File .\sunshine-import.ps1 -DryRun
  With DuckStation:
    ... -DuckStationExe "C:\Emulators\DuckStation\duckstation-qt-x64-ReleaseLTCG.exe" -Ps1Folder "D:\PS1"
#>
param(
  [string]$SunshineConfigDir = (Join-Path $env:ProgramFiles 'Sunshine\config'),
  [string]$DuckStationExe = '',
  [string]$Ps1Folder = '',
  [switch]$DryRun
)

$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Drawing

function Find-SteamPath {
  foreach ($key in @('HKCU:\Software\Valve\Steam', 'HKLM:\SOFTWARE\WOW6432Node\Valve\Steam', 'HKLM:\SOFTWARE\Valve\Steam')) {
    $item = Get-ItemProperty -Path $key -ErrorAction SilentlyContinue
    if ($item) {
      foreach ($name in @('SteamPath', 'InstallPath')) {
        $p = $item.$name
        if ($p -and (Test-Path $p)) { return ($p -replace '/', '\') }
      }
    }
  }
  return $null
}

function Get-SteamLibraries([string]$steam) {
  $libs = @($steam)
  $vdf = Join-Path $steam 'steamapps\libraryfolders.vdf'
  if (Test-Path $vdf) {
    foreach ($m in [regex]::Matches((Get-Content $vdf -Raw), '"path"\s+"([^"]+)"')) {
      $libs += ($m.Groups[1].Value -replace '\\\\', '\')
    }
  }
  return $libs | Select-Object -Unique
}

function Get-SteamGames([string[]]$libraries) {
  $skip = 'Redistributable|Steamworks|Proton|Steam Linux Runtime|SteamVR|Dedicated Server|\bSDK\b'
  $games = @()
  foreach ($lib in $libraries) {
    $apps = Join-Path $lib 'steamapps'
    if (-not (Test-Path $apps)) { continue }
    foreach ($acf in Get-ChildItem $apps -Filter 'appmanifest_*.acf') {
      $text = Get-Content $acf.FullName -Raw
      $id = [regex]::Match($text, '"appid"\s+"(\d+)"').Groups[1].Value
      $name = [regex]::Match($text, '"name"\s+"([^"]+)"').Groups[1].Value
      if (-not $id -or -not $name -or $id -eq '228980' -or $name -match $skip) { continue }
      $games += [pscustomobject]@{ Id = $id; Name = $name }
    }
  }
  return $games | Sort-Object Name -Unique
}

# Steam keeps covers either as <id>_library_600x900.jpg or in a <id>\ folder (newer clients).
function Find-SteamCover([string]$steam, [string]$id) {
  $cache = Join-Path $steam 'appcache\librarycache'
  $old = Join-Path $cache "$($id)_library_600x900.jpg"
  if (Test-Path $old) { return $old }
  $dir = Join-Path $cache $id
  if (Test-Path $dir) {
    $f = Get-ChildItem $dir -Recurse -File | Where-Object { $_.Name -like 'library_600x900*' } | Select-Object -First 1
    if ($f) { return $f.FullName }
  }
  return $null
}

function Convert-ToPng([string]$source, [string]$target) {
  $img = [System.Drawing.Image]::FromFile($source)
  try { $img.Save($target, [System.Drawing.Imaging.ImageFormat]::Png) } finally { $img.Dispose() }
}

# ---------------------------------------------------------------------------

$appsPath = Join-Path $SunshineConfigDir 'apps.json'
if (-not (Test-Path $appsPath)) { throw "apps.json introuvable : $appsPath (utilise -SunshineConfigDir)" }
$coversDir = Join-Path $SunshineConfigDir 'covers'

$json = Get-Content $appsPath -Raw | ConvertFrom-Json
$apps = @($json.apps)
$existing = @{}
foreach ($a in $apps) { $existing[$a.name] = $true }

$added = @()

$steam = Find-SteamPath
if ($steam) {
  Write-Host "Steam : $steam"
  foreach ($g in Get-SteamGames (Get-SteamLibraries $steam)) {
    if ($existing.ContainsKey($g.Name)) { Write-Host "  deja present : $($g.Name)"; continue }
    $app = [ordered]@{ name = $g.Name; detached = @("steam://rungameid/$($g.Id)") }
    $cover = Find-SteamCover $steam $g.Id
    if ($cover) {
      $png = Join-Path $coversDir "steam_$($g.Id).png"
      if (-not $DryRun) {
        New-Item -ItemType Directory -Force -Path $coversDir | Out-Null
        Convert-ToPng $cover $png
      }
      $app['image-path'] = $png
    }
    $added += [pscustomobject]$app
    Write-Host ("  + {0}{1}" -f $g.Name, $(if ($cover) { '' } else { ' (sans jaquette)' }))
  }
} else {
  Write-Host 'Steam non trouve, etape ignoree.'
}

if ($DuckStationExe -and $Ps1Folder) {
  if (-not (Test-Path $DuckStationExe)) { throw "DuckStation introuvable : $DuckStationExe" }
  Write-Host "DuckStation : $Ps1Folder"
  $files = Get-ChildItem $Ps1Folder -Recurse -File | Where-Object { $_.Extension -in '.cue', '.chd', '.m3u', '.pbp' }
  foreach ($f in $files | Sort-Object Name) {
    $name = [System.IO.Path]::GetFileNameWithoutExtension($f.Name)
    if ($existing.ContainsKey($name)) { Write-Host "  deja present : $name"; continue }
    $app = [ordered]@{ name = $name; cmd = "`"$DuckStationExe`" -batch -fullscreen -- `"$($f.FullName)`"" }
    $cover = @('.png', '.jpg') | ForEach-Object { Join-Path $f.DirectoryName "$name$_" } | Where-Object { Test-Path $_ } | Select-Object -First 1
    if ($cover) {
      $png = Join-Path $coversDir ("ps1_" + ($name -replace '[^\w\-]', '_') + '.png')
      if (-not $DryRun) {
        New-Item -ItemType Directory -Force -Path $coversDir | Out-Null
        if ($cover -like '*.png') { Copy-Item $cover $png -Force } else { Convert-ToPng $cover $png }
      }
      $app['image-path'] = $png
    }
    $added += [pscustomobject]$app
    Write-Host "  + $name"
  }
}

if ($added.Count -eq 0) { Write-Host 'Rien a ajouter.'; exit 0 }
if ($DryRun) { Write-Host "$($added.Count) jeux seraient ajoutes (mode -DryRun, rien n'a ete ecrit)."; exit 0 }

$backup = "$appsPath.bak-$(Get-Date -Format 'yyyyMMdd-HHmmss')"
Copy-Item $appsPath $backup
$json.apps = @($apps + $added)
# UTF-8 without BOM: Sunshine's JSON parser does not expect a BOM.
[System.IO.File]::WriteAllText($appsPath, ($json | ConvertTo-Json -Depth 10), (New-Object System.Text.UTF8Encoding $false))
Write-Host "$($added.Count) jeux ajoutes. Sauvegarde : $backup"

$svc = Get-Service -Name 'SunshineService' -ErrorAction SilentlyContinue
if ($svc) {
  Restart-Service -Name 'SunshineService'
  Write-Host 'Sunshine redemarre : la liste est a jour.'
} else {
  Write-Host 'Redemarre Sunshine pour qu''il relise la liste des jeux.'
}
