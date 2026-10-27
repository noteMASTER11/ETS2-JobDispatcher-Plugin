param([string]$GameDirectory)
$ErrorActionPreference='Stop'
$root=Split-Path $PSScriptRoot -Parent
$record=Join-Path $root 'data\installation.json'
if(-not $GameDirectory -and (Test-Path -LiteralPath $record)) {
    $GameDirectory=(Get-Content -LiteralPath $record -Raw | ConvertFrom-Json).game
}
if(-not $GameDirectory) { $GameDirectory=Read-Host 'Enter the ETS2 installation directory' }
$gameExecutable=Join-Path $GameDirectory 'bin\win_x64\eurotrucks2.exe'
if(Get-Process -Name eurotrucks2 -ErrorAction SilentlyContinue | Where-Object {$_.Path -eq $gameExecutable}) { throw 'Close ETS2 before uninstalling.' }
$plugins=Join-Path $GameDirectory 'bin\win_x64\plugins'
foreach($name in @('JobDispatcherSP.dll','JobDispatcherSP.ini')) {
    $file=Join-Path $plugins $name
    if(Test-Path -LiteralPath $file) { Remove-Item -LiteralPath $file }
}
Write-Host 'Plugin removed. Your local data/cache folder is retained.'
