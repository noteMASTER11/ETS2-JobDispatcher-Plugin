[CmdletBinding()]
param([string]$GameDirectory,[string]$DataDirectory,[int]$Workers=4,[string[]]$ModArchive=@())
$ErrorActionPreference='Stop'
$packageRoot=Split-Path $PSScriptRoot -Parent
$dropRoot=Join-Path $packageRoot 'bin\win_x64\plugins'
$bundle=if(Test-Path (Join-Path $dropRoot 'JobDispatcherSP\runtime')){Join-Path $dropRoot 'JobDispatcherSP'}else{$packageRoot}
$sourceDll=if($bundle -eq $packageRoot){Join-Path $packageRoot 'bin\JobDispatcherSP.dll'}else{Join-Path $dropRoot 'JobDispatcherSP.dll'}
if(-not (Test-Path -LiteralPath $sourceDll)){throw 'Plugin DLL missing. Download the complete offline release archive.'}
foreach($name in @('python\python.exe','converter_pix.exe')){
 if(-not(Test-Path -LiteralPath (Join-Path $bundle ('runtime\'+$name)))){throw 'Offline runtime missing. Download the complete release archive, or run tools/fetch-offline-runtime.ps1 while preparing a developer build.'}
}
if(-not $GameDirectory){
 if(Test-Path (Join-Path $packageRoot 'bin\win_x64\eurotrucks2.exe')){$GameDirectory=$packageRoot}
 else{
  $steam=(Get-ItemProperty 'HKCU:\Software\Valve\Steam' -ErrorAction SilentlyContinue).SteamPath
  $candidates=@()
  if($steam){
   $candidates+=Join-Path $steam 'steamapps\common\Euro Truck Simulator 2'
   $libraries=Join-Path $steam 'steamapps\libraryfolders.vdf'
   if(Test-Path $libraries){foreach($match in [regex]::Matches([IO.File]::ReadAllText($libraries),'"path"\s+"([^"]+)"')){
    $candidates+=Join-Path ($match.Groups[1].Value.Replace('\\','\')) 'steamapps\common\Euro Truck Simulator 2'
   }}
  }
  $GameDirectory=$candidates|Where-Object{Test-Path -LiteralPath (Join-Path $_ 'bin\win_x64\eurotrucks2.exe')}|Select-Object -First 1
  if(-not $GameDirectory){$GameDirectory=Read-Host 'Enter the ETS2 installation directory'}
 }
}
$GameDirectory=[IO.Path]::GetFullPath($GameDirectory.Trim('"'))
$exe=Join-Path $GameDirectory 'bin\win_x64\eurotrucks2.exe'
if(-not(Test-Path -LiteralPath $exe)){throw "ETS2 executable not found: $exe"}
if((Get-FileHash -LiteralPath $exe).Hash -ne 'B70C2357CC56EB5834EE47FEA3C60B2F2D4591383FF2AC479CFD3CD254301B8C'){throw 'This release supports ETS2 1.61.1.9 Windows x64 only.'}
if(Get-Process eurotrucks2 -ErrorAction SilentlyContinue|Where-Object{$_.Path -eq $exe}){throw 'Close this ETS2 installation before replacing its plugin.'}
$plugins=Join-Path $GameDirectory 'bin\win_x64\plugins'
$targetBundle=Join-Path $plugins 'JobDispatcherSP'
if(-not $DataDirectory){$DataDirectory=Join-Path $targetBundle 'data'}
$DataDirectory=[IO.Path]::GetFullPath($DataDirectory)
New-Item -ItemType Directory -Path $plugins,$targetBundle,$DataDirectory -Force|Out-Null
$targetDll=Join-Path $plugins 'JobDispatcherSP.dll'
if([IO.Path]::GetFullPath($bundle) -ne [IO.Path]::GetFullPath($targetBundle)){
 foreach($name in @('runtime','tools','licenses','docs')){Copy-Item -LiteralPath (Join-Path $bundle $name) -Destination $targetBundle -Recurse -Force}
}
$uiSource=if($bundle -eq $packageRoot){Join-Path $bundle 'assets'}else{Join-Path $bundle 'data'}
if([IO.Path]::GetFullPath($uiSource) -ne $DataDirectory){Get-ChildItem -LiteralPath $uiSource -Filter 'ui-labels.*.tsv' -File|Copy-Item -Destination $DataDirectory -Force}
if([IO.Path]::GetFullPath($sourceDll) -ne [IO.Path]::GetFullPath($targetDll)){
 if(Test-Path $targetDll){
  $backups=Join-Path $DataDirectory 'backups';New-Item -ItemType Directory $backups -Force|Out-Null
  Copy-Item -LiteralPath $targetDll -Destination (Join-Path $backups ('JobDispatcherSP-'+[DateTime]::UtcNow.ToString('yyyyMMdd-HHmmssfff')+'.dll'))
 }
 Copy-Item -LiteralPath $sourceDll -Destination $targetDll -Force
}
[IO.File]::WriteAllText((Join-Path $plugins 'JobDispatcherSP.ini'),"[JobDispatcherSP]`r`nDataDirectory=$DataDirectory`r`n",[Text.Encoding]::Unicode)
@{workers=[Math]::Max(1,$Workers);extra_mods=@($ModArchive|ForEach-Object{[IO.Path]::GetFullPath($_)})}|ConvertTo-Json|Set-Content -LiteralPath (Join-Path $DataDirectory 'offline-settings.json') -Encoding utf8
@{version='0.1';game=$GameDirectory;dll=$targetDll;data=$DataDirectory;sha256=(Get-FileHash -LiteralPath $targetDll).Hash}|ConvertTo-Json|Set-Content -LiteralPath (Join-Path $DataDirectory 'installation.json') -Encoding utf8
Write-Host "Installed: $targetDll"
Write-Host 'No Internet connection is needed. Load a profile; models and translations will be prepared locally once.'