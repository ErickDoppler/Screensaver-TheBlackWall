# Installs the built screensaver for the current user (no admin needed) and
# makes it the active screensaver. Run from a normal PowerShell.
$scr = Join-Path $PSScriptRoot '..\build\win-mingw\TheBlackWall.scr'
if (-not (Test-Path $scr)) { Write-Error 'Build first: tools\build-windows.cmd'; exit 1 }
$dest = Join-Path $env:LOCALAPPDATA 'TheBlackWall'
New-Item -ItemType Directory -Force $dest | Out-Null
Copy-Item $scr $dest -Force
$target = Join-Path $dest 'TheBlackWall.scr'
Set-ItemProperty 'HKCU:\Control Panel\Desktop' -Name 'SCRNSAVE.EXE' -Value $target
Set-ItemProperty 'HKCU:\Control Panel\Desktop' -Name 'ScreenSaveActive' -Value '1'
Write-Host "Installed $target and selected it as the active screensaver."
Write-Host 'Open "Change screen saver" in Windows settings to adjust the timeout and options.'
