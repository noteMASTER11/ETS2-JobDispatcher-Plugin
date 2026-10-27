[CmdletBinding()]
param()
$ErrorActionPreference='Stop'
$root=Split-Path $PSScriptRoot -Parent
$binary=Join-Path $root 'bin\JobDispatcherSP.dll'
if(-not (Test-Path -LiteralPath $binary)) { throw 'Build the plugin and copy its DLL into bin first.' }
$package=Join-Path $root ('package\JobDispatcherSP-'+[Guid]::NewGuid().ToString('N'))
$dist=Join-Path $root 'dist'
New-Item -ItemType Directory -Path (Join-Path $package 'bin'),$dist -Force | Out-Null
Copy-Item -LiteralPath $binary -Destination (Join-Path $package 'bin') -Force
foreach($name in @('assets','licenses','docs')) {
    Copy-Item -LiteralPath (Join-Path $root $name) -Destination $package -Recurse -Force
}
New-Item -ItemType Directory -Path (Join-Path $package 'tools') -Force | Out-Null
Get-ChildItem -LiteralPath (Join-Path $root 'tools') -File | Copy-Item -Destination (Join-Path $package 'tools') -Force
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
