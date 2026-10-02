<#
.SYNOPSIS
  Build -> stage on Azahar's emulated SD -> launch -> wait -> collect -> close.

.DESCRIPTION
  Windows host script (Azahar needs the host GPU); the build runs in WSL. The game folder staged in WSL
  (-SdInWsl: data.win + gfx/ from n3ds-preprocess) is mirrored to <azahar>\user\sdmc\3ds\butterscotch,
  the optional -Harness file becomes harness.txt (scripted presses, screenshots, exit). The run's
  butterscotch.log, shots and Azahar's log are copied to -ArtifactsDir\<timestamp>\.

.EXAMPLE
  powershell -ExecutionPolicy Bypass -File tools\n3ds\run_in_azahar.ps1 -Harness tools\n3ds\harness\title.txt -WaitSeconds 120
#>
param(
    [int]$WaitSeconds = 120,
    [switch]$NoBuild,
    [string]$Harness = "",
    [string]$AzaharDir = $(if ($env:AZAHAR_DIR) { $env:AZAHAR_DIR } else { "$env:USERPROFILE\Desktop\KeeperFx_3DS\tools\azahar\azahar-windows-msvc-2126.1.2" }),
    [string]$ArtifactsDir = $(if ($env:BS3DS_ARTIFACTS) { $env:BS3DS_ARTIFACTS } else { "$env:USERPROFILE\Desktop\am2r3ds\runs" }),
    [string]$Distro = "kfx-ubuntu",
    [string]$RepoInWsl = "/root/am2r3ds/butterscotch",
    [string]$BuildInWsl = "/root/am2r3ds/build-n3ds",
    [string]$SdInWsl = "/root/am2r3ds/sd/3ds/butterscotch",
    # Azahar does not emulate the New 3DS 804 MHz mode; 300% approximates it.
    [int]$CpuClock = 300,
    [string]$LogFilter = "*:Info",
    [string]$Label = "run"
)
$ErrorActionPreference = "Stop"

$exe = Join-Path $AzaharDir "azahar.exe"
if (-not (Test-Path $exe)) { throw "azahar.exe not found in $AzaharDir" }
$user = Join-Path $AzaharDir "user"
$sdmc = Join-Path $user "sdmc"
$gameDir = Join-Path $sdmc "3ds\butterscotch"

if (-not $NoBuild) {
    & wsl.exe -d $Distro -u root -- bash "$RepoInWsl/tools/n3ds/build.sh" $BuildInWsl
    if ($LASTEXITCODE -ne 0) { throw "build failed ($LASTEXITCODE)" }
}
$built = "\\wsl.localhost\$Distro" + ($BuildInWsl -replace "/", "\") + "\butterscotch.3dsx"
if (-not (Test-Path $built)) { throw "no build output at $built" }

# Stage: copy the game folder over (no deletes: saves and config the game wrote stay).
New-Item -ItemType Directory -Force $gameDir | Out-Null
$stageWin = "\\wsl.localhost\$Distro" + ($SdInWsl -replace "/", "\")
& robocopy $stageWin $gameDir /E /XD shots audio /XF butterscotch.log done.txt harness.txt atlas_trace.log /NFL /NDL /NJH /NJS /NP /R:1 /W:1 | Out-Null
if ($LASTEXITCODE -ge 8) { throw "robocopy failed ($LASTEXITCODE)" }
Copy-Item -Force $built (Join-Path $gameDir "butterscotch.3dsx")
Remove-Item -Force -Recurse -ErrorAction SilentlyContinue (Join-Path $gameDir "done.txt"), (Join-Path $gameDir "shots"), (Join-Path $gameDir "butterscotch.log"), (Join-Path $gameDir "harness.txt")
if ($Harness -ne "") { Copy-Item -Force $Harness (Join-Path $gameDir "harness.txt") }

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
$stamp = Get-Date -Format "yyyyMMdd-HHmmss"
$start = Get-Date
$proc = Start-Process -FilePath $exe -ArgumentList "`"$(Join-Path $gameDir 'butterscotch.3dsx')`"" -PassThru
Write-Host "Launched (pid $($proc.Id)); waiting up to $WaitSeconds s"

$dest = Join-Path $ArtifactsDir "$stamp-$Label"
New-Item -ItemType Directory -Force $dest | Out-Null

$doneFile = Join-Path $gameDir "done.txt"
$result = "timeout"
while (((Get-Date) - $start).TotalSeconds -lt $WaitSeconds) {
    Start-Sleep -Seconds 1
    if ($proc.HasExited) { $result = "emulator-exited"; break }
    if (Test-Path $doneFile) { $result = "done"; Start-Sleep -Seconds 1; break }
}
$elapsed = [int]((Get-Date) - $start).TotalSeconds
if (-not $proc.HasExited) { Stop-Process -Id $proc.Id -Force }
Start-Sleep -Milliseconds 500

foreach ($f in @("butterscotch.log", "done.txt", "harness.txt")) {
    Copy-Item -Force (Join-Path $gameDir $f) $dest -ErrorAction SilentlyContinue
}
Copy-Item -Force (Join-Path $gameDir "shots\*.png") $dest -ErrorAction SilentlyContinue
Copy-Item -Force (Join-Path $user "log\azahar_log.txt") (Join-Path $dest "azahar_log.txt") -ErrorAction SilentlyContinue
"result=$result elapsed=${elapsed}s" | Set-Content -Encoding utf8 (Join-Path $dest "run.txt")
Write-Host "Result: $result after ${elapsed}s. Artifacts: $dest"
Get-ChildItem $dest | Format-Table Name, Length -AutoSize | Out-String | Write-Host
