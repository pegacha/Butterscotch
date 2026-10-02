<#
.SYNOPSIS
  Build -> stage on Azahar's emulated SD -> install -> launch -> wait -> collect -> close.

.DESCRIPTION
  Windows host script (Azahar needs the host GPU); the build runs in WSL. The game folder staged in WSL
  (-SdInWsl, made by tools/n3ds/make_sd.sh) is copied to <azahar>\user\sdmc\3ds\<game>; the game's .cia is
  installed (azahar -i) and the installed title booted, as on a console (-Use3dsx boots the .3dsx instead).
  The optional -Harness file becomes harness.txt (scripted presses, screenshots, exit). The run's log.txt,
  shots and Azahar's log are copied to -ArtifactsDir\<timestamp>\.

.EXAMPLE
  powershell -ExecutionPolicy Bypass -File tools\n3ds\run_in_azahar.ps1 -Harness tools\n3ds\harness\explore.txt -Save <dir>
#>
param(
    # Hard limit: the emulator is killed after this many seconds even if the harness has not finished (0 = none:
    # the run ends when the harness exits or Azahar is closed).
    [int]$WaitSeconds = 150,
    # Folder with save files (sav1, config.ini, ...) copied into the game folder before launch, e.g. a known checkpoint.
    [string]$Save = "",
    # Start without any save files (fresh new game).
    [switch]$ClearSaves,
    # Keyboard input playback (desktop --playback-inputs JSON, e.g. from ltm_to_inputs.py), copied as inputs.json.
    [string]$Inputs = "",
    [switch]$NoBuild,
    [string]$Harness = "",
    [string]$AzaharDir = $(if ($env:AZAHAR_DIR) { $env:AZAHAR_DIR } else { "$env:USERPROFILE\Desktop\KeeperFx_3DS\tools\azahar\azahar-windows-msvc-2126.1.2" }),
    [string]$ArtifactsDir = $(if ($env:BS3DS_ARTIFACTS) { $env:BS3DS_ARTIFACTS } else { "$env:USERPROFILE\Desktop\am2r3ds\runs" }),
    [string]$Distro = "kfx-ubuntu",
    [string]$RepoInWsl = "/root/am2r3ds/butterscotch",
    [string]$BuildInWsl = "/root/am2r3ds/build-n3ds",
    [string]$SdInWsl = "",
    # Azahar does not emulate the New 3DS 804 MHz mode; 300% approximates it.
    [int]$CpuClock = 300,
    [string]$LogFilter = "*:Info",
    [string]$Label = "run",
    # Output/SD folder name of the game build (tools/n3ds/build.sh profile) and its CIA title ID.
    [string]$Game = "am2r",
    [string]$TitleId = "000400000a2e2100",
    [switch]$Use3dsx
)
$ErrorActionPreference = "Stop"

$exe = Join-Path $AzaharDir "azahar.exe"
if (-not (Test-Path $exe)) { throw "azahar.exe not found in $AzaharDir" }
$user = Join-Path $AzaharDir "user"
$sdmc = Join-Path $user "sdmc"
$gameDir = Join-Path $sdmc "3ds\$Game"
if ($SdInWsl -eq "") { $SdInWsl = "/root/am2r3ds/sd/3ds/$Game" }

if (-not $NoBuild) {
    & wsl.exe -d $Distro -u root -- bash "$RepoInWsl/tools/n3ds/build.sh" $BuildInWsl
    if ($LASTEXITCODE -ne 0) { throw "build failed ($LASTEXITCODE)" }
}
$buildWin = "\\wsl.localhost\$Distro" + ($BuildInWsl -replace "/", "\")
$built = Join-Path $buildWin "$Game.3dsx"
$builtCia = Join-Path $buildWin "$Game.cia"
if (-not (Test-Path $built)) { throw "no build output at $built" }

# Stage: copy the game folder over (no deletes: saves and config the game wrote stay).
New-Item -ItemType Directory -Force $gameDir | Out-Null
$stageWin = "\\wsl.localhost\$Distro" + ($SdInWsl -replace "/", "\")
& robocopy $stageWin $gameDir /E /XD shots /XF log.txt done.txt harness.txt inputs.json atlas_trace.log /NFL /NDL /NJH /NJS /NP /R:1 /W:1 | Out-Null
if ($LASTEXITCODE -ge 8) { throw "robocopy failed ($LASTEXITCODE)" }
if ($Use3dsx) { Copy-Item -Force $built (Join-Path $gameDir "$Game.3dsx") }
Remove-Item -Force -Recurse -ErrorAction SilentlyContinue (Join-Path $gameDir "done.txt"), (Join-Path $gameDir "shots"), (Join-Path $gameDir "log.txt"), (Join-Path $gameDir "harness.txt")
if ($Harness -ne "") { Copy-Item -Force $Harness (Join-Path $gameDir "harness.txt") }
Remove-Item -Force -ErrorAction SilentlyContinue (Join-Path $gameDir "inputs.json")
if ($Inputs -ne "") { Copy-Item -Force $Inputs (Join-Path $gameDir "inputs.json") }
if ($ClearSaves) { Remove-Item -Force -ErrorAction SilentlyContinue (Join-Path $gameDir "sav*"), (Join-Path $gameDir "config.ini") }
if ($Save -ne "") {
    Remove-Item -Force -ErrorAction SilentlyContinue (Join-Path $gameDir "sav*")
    Copy-Item -Force (Join-Path $Save "*") $gameDir
}

# Azahar settings: New 3DS, Vulkan (OpenGL hangs the UI on this host), no close prompt, CPU clock, log filter.
$cfg = Join-Path $user "config\qt-config.ini"
if (Test-Path $cfg) {
    $t = [IO.File]::ReadAllText($cfg)
    foreach ($kv in @(@("graphics_api", "2"), @("is_new_3ds", "true"), @("confirmClose", "false"), @("cpu_clock_percentage", "$CpuClock"), @("log_filter", $LogFilter))) {
        $k = $kv[0]; $v = $kv[1]
        $t = $t -replace "(?m)^$k\\default=true", "$k\default=false"
        $t = $t -replace "(?m)^$k=.*$", "$k=$v"
    }
    [IO.File]::WriteAllText($cfg, $t)
}

Get-Process azahar -ErrorAction SilentlyContinue | Stop-Process -Force
Start-Sleep -Milliseconds 500
$deployed = Join-Path $gameDir "$Game.3dsx"
if (-not $Use3dsx) {
    # Install the CIA (azahar -i stays open afterwards), then boot the installed title.
    if (-not (Test-Path $builtCia)) { throw "no CIA at $builtCia" }
    $inst = Start-Process -FilePath $exe -ArgumentList "-i", "`"$builtCia`"" -PassThru
    Start-Sleep -Seconds 8
    if (-not $inst.HasExited) { $inst | Stop-Process -Force }
    Start-Sleep -Milliseconds 500
    $hi = $TitleId.Substring(0, 8); $lo = $TitleId.Substring(8)
    $app = Get-ChildItem -Recurse (Join-Path $sdmc "Nintendo 3DS") -Filter "*.app" -ErrorAction SilentlyContinue |
        Where-Object { $_.FullName -like "*\title\$hi\$lo\content\*" } | Sort-Object LastWriteTime -Descending | Select-Object -First 1
    if (-not $app) { throw "CIA install did not produce title $TitleId" }
    $deployed = $app.FullName
}
$stamp = Get-Date -Format "yyyyMMdd-HHmmss"
$start = Get-Date
$proc = Start-Process -FilePath $exe -ArgumentList "`"$deployed`"" -PassThru
if ($WaitSeconds -gt 0) { Write-Host "Launched (pid $($proc.Id)); waiting up to $WaitSeconds s" } else { Write-Host "Launched (pid $($proc.Id)); no time limit" }

$dest = Join-Path $ArtifactsDir "$stamp-$Label"
New-Item -ItemType Directory -Force $dest | Out-Null

$doneFile = Join-Path $gameDir "done.txt"
$result = "timeout"
while ($WaitSeconds -le 0 -or ((Get-Date) - $start).TotalSeconds -lt $WaitSeconds) {
    Start-Sleep -Seconds 1
    if ($proc.HasExited) { $result = "emulator-exited"; break }
    if (Test-Path $doneFile) { $result = "done"; Start-Sleep -Seconds 1; break }
}
$elapsed = [int]((Get-Date) - $start).TotalSeconds
if (-not $proc.HasExited) { Stop-Process -Id $proc.Id -Force }
Start-Sleep -Milliseconds 500

foreach ($f in @("log.txt", "done.txt", "harness.txt")) {
    Copy-Item -Force (Join-Path $gameDir $f) $dest -ErrorAction SilentlyContinue
}
Copy-Item -Force (Join-Path $gameDir "shots\*.png") $dest -ErrorAction SilentlyContinue
Copy-Item -Force (Join-Path $user "log\azahar_log.txt") (Join-Path $dest "azahar_log.txt") -ErrorAction SilentlyContinue
"result=$result elapsed=${elapsed}s" | Set-Content -Encoding utf8 (Join-Path $dest "run.txt")
Write-Host "Result: $result after ${elapsed}s. Artifacts: $dest"
Get-ChildItem $dest | Format-Table Name, Length -AutoSize | Out-String | Write-Host
