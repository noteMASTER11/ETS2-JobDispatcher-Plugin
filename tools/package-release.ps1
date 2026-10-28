[CmdletBinding()]
param()
$ErrorActionPreference='Stop'
$root=Split-Path $PSScriptRoot -Parent
$binary=Join-Path $root 'bin\JobDispatcherSP.dll'
if(-not (Test-Path -LiteralPath $binary)) { throw 'Build the plugin and copy its DLL into bin first.' }
$package=Join-Path $root ('package\JobDispatcherSP-'+[Guid]::NewGuid().ToString('N'))
$plugins=Join-Path $package 'bin\win_x64\plugins'
$bundle=Join-Path $plugins 'JobDispatcherSP'
$dist=Join-Path $root 'dist'
New-Item -ItemType Directory -Path $plugins,(Join-Path $bundle 'data'),$dist -Force | Out-Null
Copy-Item -LiteralPath $binary -Destination $plugins -Force
[IO.File]::WriteAllText((Join-Path $plugins 'JobDispatcherSP.ini'),"[JobDispatcherSP]`r`nDataDirectory=auto`r`n",[Text.Encoding]::Unicode)
foreach($name in @('runtime','licenses','docs')) {
    if(-not (Test-Path -LiteralPath (Join-Path $root $name))){throw "Package component missing: $name"}
    Copy-Item -LiteralPath (Join-Path $root $name) -Destination $bundle -Recurse -Force
}
Get-ChildItem -LiteralPath (Join-Path $root 'assets') -Filter 'ui-labels.*.tsv' -File | Copy-Item -Destination (Join-Path $bundle 'data') -Force
New-Item -ItemType Directory -Path (Join-Path $bundle 'tools') -Force | Out-Null
foreach($name in Get-Content -LiteralPath (Join-Path $PSScriptRoot 'runtime-tools.txt')){
    Copy-Item -LiteralPath (Join-Path $PSScriptRoot $name) -Destination (Join-Path $bundle 'tools') -Force
}
New-Item -ItemType Directory -Path (Join-Path $package 'tools') -Force | Out-Null
foreach($name in @('install-portable.ps1','install-settings.ps1','runtime-tools.txt','uninstall-portable.ps1')){Copy-Item -LiteralPath (Join-Path $root ('tools\'+$name)) -Destination (Join-Path $package 'tools') -Force}
foreach($name in @('README.md','Install.cmd','Uninstall.cmd')) {
    Copy-Item -LiteralPath (Join-Path $root $name) -Destination $package -Force
}
Compress-Archive -Path (Join-Path $package '*') -DestinationPath (Join-Path $dist 'JobDispatcherSP-0.1-Windows-x64.zip') -CompressionLevel Optimal -Force
git -C $root archive --format=zip --prefix=ETS2-JobDispatcher-Plugin-0.1/ -o (Join-Path $dist 'ETS2-JobDispatcher-Plugin-0.1-source.zip') HEAD
if($LASTEXITCODE) { throw 'Source snapshot creation failed.' }
Get-ChildItem -LiteralPath $dist -Filter '*.zip' -File | ForEach-Object {
    '{0}  {1}' -f (Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash.ToLower(),$_.Name
} | Set-Content -LiteralPath (Join-Path $dist 'SHA256SUMS.txt') -Encoding utf8
Get-ChildItem -LiteralPath $dist -File | Select-Object Name,Length
