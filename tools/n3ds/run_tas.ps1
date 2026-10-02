<#
.SYNOPSIS
  Replays a libTAS movie (.ltm) on the 3DS build in Azahar and collects screenshots, log and frame times.

.DESCRIPTION
  Converts the movie with tools/n3ds/ltm_to_inputs.py (in WSL), writes a harness that takes a screenshot every
  -ShotEvery frames and quits shortly after the movie ends, then runs tools/n3ds/run_in_azahar.ps1 with no saves
  (a TAS starts from a fresh game) and the fixed seed that input playback uses.

.EXAMPLE
  powershell -ExecutionPolicy Bypass -File \\wsl.localhost\kfx-ubuntu\root\am2r3ds\butterscotch\tools\n3ds\run_tas.ps1
  powershell -ExecutionPolicy Bypass -File ...\run_tas.ps1 -Movie C:\path\to\movie.ltm -ShotEvery 120 -Build
#>
param(
    # The movie: a Windows path or a path inside WSL. Default: the AM2R Crash% movie from tasvideos.org.
    [string]$Movie = "/root/am2r3ds/tas/crash.ltm",
    [int]$ShotEvery = 300,
    # Keep running this many frames after the last input (the game keeps going on its own).
    [int]$TailFrames = 60,
    # Hard limit for the emulator run.
    [int]$WaitSeconds = 240,
    # Rebuild the CIA first (default: use the last build).
    [switch]$Build,
    [string]$Distro = "kfx-ubuntu",
    [string]$RepoInWsl = "/root/am2r3ds/butterscotch"
)
$ErrorActionPreference = "Stop"
$repoWin = "\\wsl.localhost\$Distro" + ($RepoInWsl -replace "/", "\")

# The movie as a WSL path.
$movieWsl = $Movie
if ($Movie -match '^[A-Za-z]:\\' -or $Movie.StartsWith("\\")) {
    $movieWsl = (& wsl.exe -d $Distro -u root -- wslpath -a "$Movie").Trim()
}
$name = [IO.Path]::GetFileNameWithoutExtension($movieWsl)
$work = "/root/am2r3ds/tas/runs"
$jsonWsl = "$work/$name.json"
$harnessWsl = "$work/$name.harness.txt"

# Convert and write the harness.
$out = & wsl.exe -d $Distro -u root -- bash -c "mkdir -p '$work' && python3 '$RepoInWsl/tools/n3ds/ltm_to_inputs.py' '$movieWsl' '$jsonWsl'"
if ($LASTEXITCODE -ne 0) { throw "could not convert $Movie" }
Write-Host "Movie: $out"
$frames = [int](($out -split " ")[0])
$end = $frames + $TailFrames
$lines = @("# $name ($frames frames): screenshot every $ShotEvery frames")
for ($f = $ShotEvery; $f -lt $end; $f += $ShotEvery) { $lines += "screenshot $f" }
$lines += "screenshot $end"
$lines += "exit $($end + 2)"
$harnessWin = "\\wsl.localhost\$Distro" + ($harnessWsl -replace "/", "\")
[IO.File]::WriteAllText($harnessWin, ($lines -join "`n") + "`n")

# Run.
$runArgs = @{
    ClearSaves = $true
    Inputs = "\\wsl.localhost\$Distro" + ($jsonWsl -replace "/", "\")
    Harness = $harnessWin
    WaitSeconds = $WaitSeconds
    Label = "tas-$name"
}
if (-not $Build) { $runArgs.NoBuild = $true }
& (Join-Path $repoWin "tools\n3ds\run_in_azahar.ps1") @runArgs

# Summary from the newest run.
$artifacts = if ($env:BS3DS_ARTIFACTS) { $env:BS3DS_ARTIFACTS } else { "$env:USERPROFILE\Desktop\am2r3ds\runs" }
$dir = (Get-ChildItem $artifacts -Directory | Where-Object Name -like "*-tas-$name" | Sort-Object Name | Select-Object -Last 1).FullName
if ($dir -and (Test-Path "$dir\log.txt")) {
    Write-Host "`nRooms and frame times:"
    Select-String -Path "$dir\log.txt" -Pattern "Room [0-9]+:|Perf:|UNIMPL summary|Error" | ForEach-Object { $_.Line -replace ', heap used.*', '' } | Write-Host
    Write-Host "`nScreenshots, log.txt and azahar_log.txt: $dir"
}
