# Installs the built screensaver for the current user (no admin needed) and
# makes it the active screensaver. Run from a normal PowerShell.
#
# The full path of a permanent copy is what goes into the registry. Explorer's
# right-click "Install" instead pins the .scr where it happens to sit, so a
# copy in the build folder or in Downloads stops working once it is gone, and
# Windows then does nothing at all on idle, without a word.
$scr = Join-Path $PSScriptRoot '..\build\win-mingw\TheBlackWall.scr'
if (-not (Test-Path $scr)) { Write-Error 'Build first: 2-build-for-win64.cmd'; exit 1 }

$key = 'HKCU:\Control Panel\Desktop'
$previous = (Get-ItemProperty $key -Name 'SCRNSAVE.EXE' -ErrorAction SilentlyContinue).'SCRNSAVE.EXE'

$dest = Join-Path $env:LOCALAPPDATA 'TheBlackWall'
New-Item -ItemType Directory -Force $dest | Out-Null
Copy-Item $scr $dest -Force
$target = Join-Path $dest 'TheBlackWall.scr'

Set-ItemProperty $key -Name 'SCRNSAVE.EXE' -Value $target
Set-ItemProperty $key -Name 'ScreenSaveActive' -Value '1'
# Without a timeout Windows never starts a screensaver; 10 minutes if unset.
$timeout = (Get-ItemProperty $key -Name 'ScreenSaveTimeOut' -ErrorAction SilentlyContinue).ScreenSaveTimeOut
if (-not $timeout -or [int]$timeout -le 0) {
    $timeout = '600'
    Set-ItemProperty $key -Name 'ScreenSaveTimeOut' -Value $timeout
}

Write-Host "Installed $target and selected it as the active screensaver."
if ($previous -and $previous -ne $target -and -not (Test-Path $previous)) {
    Write-Host "Note: Windows had been pointed at $previous, which does not exist -"
    Write-Host '      that is why nothing happened on idle. It is fixed now.'
}
Write-Host "It starts after $timeout seconds of no input."
Write-Host 'Open "Change screen saver" in Windows settings to adjust the timeout.'
Write-Host 'The dropdown there only lists screensavers in C:\Windows\System32, so'
Write-Host 'this one is not in it; picking another entry replaces this setting.'
