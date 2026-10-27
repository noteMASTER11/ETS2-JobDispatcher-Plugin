[CmdletBinding()]
param([string]$GameDirectory, [string]$DataDirectory, [int]$Workers=[Environment]::ProcessorCount, [string[]]$ModArchive=@())
$ErrorActionPreference='Stop'
$packageRoot=Split-Path $PSScriptRoot -Parent
if(-not $GameDirectory) {
    $steam=(Get-ItemProperty 'HKCU:\Software\Valve\Steam' -ErrorAction SilentlyContinue).SteamPath
    $candidates=@()
    if($steam) {
        $candidates+=Join-Path $steam 'steamapps\common\Euro Truck Simulator 2'
        $libraries=Join-Path $steam 'steamapps\libraryfolders.vdf'
        if(Test-Path -LiteralPath $libraries) {
            foreach($match in [regex]::Matches([IO.File]::ReadAllText($libraries),'"path"\s+"([^"]+)"')) {
                $library=$match.Groups[1].Value.Replace('\\','\')
                $candidates+=Join-Path $library 'steamapps\common\Euro Truck Simulator 2'
            }
        }
    }
    $GameDirectory=$candidates | Where-Object {Test-Path -LiteralPath (Join-Path $_ 'bin\win_x64\eurotrucks2.exe')} | Select-Object -First 1
    if(-not $GameDirectory) { $GameDirectory=Read-Host 'Enter the Euro Truck Simulator 2 installation directory' }
}
$GameDirectory=[IO.Path]::GetFullPath($GameDirectory.Trim('"'))
$gameExecutable=Join-Path $GameDirectory 'bin\win_x64\eurotrucks2.exe'
if(-not (Test-Path -LiteralPath $gameExecutable)) { throw "ETS2 executable not found: $gameExecutable" }
if((Get-FileHash -LiteralPath $gameExecutable -Algorithm SHA256).Hash -ne 'B70C2357CC56EB5834EE47FEA3C60B2F2D4591383FF2AC479CFD3CD254301B8C') {
    throw 'This release supports ETS2 1.61.1.9 Windows x64 only. The executable differs.'
}
if(Get-Process -Name eurotrucks2 -ErrorAction SilentlyContinue | Where-Object {$_.Path -eq $gameExecutable}) {
    throw 'Close this ETS2 installation, then run Install.cmd again.'
}
if(-not $DataDirectory) { $DataDirectory=Join-Path $packageRoot 'data' }
$DataDirectory=[IO.Path]::GetFullPath($DataDirectory)
$tempDirectory=Join-Path $DataDirectory 'temp'
$toolsDirectory=Join-Path $DataDirectory 'tools'
New-Item -ItemType Directory -Path $tempDirectory,$toolsDirectory -Force | Out-Null
$env:TEMP=$tempDirectory; $env:TMP=$tempDirectory; $env:PYTHONIOENCODING='utf-8'
[Net.ServicePointManager]::SecurityProtocol=[Net.SecurityProtocolType]::Tls12
function Get-CheckedFile([string]$Uri,[string]$Path,[string]$Hash) {
    if(-not (Test-Path -LiteralPath $Path)) {
        Write-Host "Downloading $(Split-Path $Path -Leaf)..."
        Invoke-WebRequest -Uri $Uri -OutFile $Path -UseBasicParsing
    }
    if((Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash -ne $Hash) {
        throw "Download checksum mismatch: $Path. Remove this file and retry."
    }
}
$converter=Join-Path $toolsDirectory 'converter_pix.exe'
Get-CheckedFile 'https://raw.githubusercontent.com/mwl4/ConverterPIX/3cd4e73a86d0c6bd28e117664c50a36cabeccf38/bin/win_x64/converter_pix.exe' $converter '60310EE8E01563145B71A8FB62F63D7A8B03906334E84E1463918F615F192ED2'
$pythonArchive=Join-Path $toolsDirectory 'python-3.13.1-embed-amd64.zip'
Get-CheckedFile 'https://www.python.org/ftp/python/3.13.1/python-3.13.1-embed-amd64.zip' $pythonArchive '7B7923FF0183A8B8FCA90F6047184B419B108CB437F75FC1C002F9D2F8BCEC16'
$pythonDirectory=Join-Path $toolsDirectory 'python'
if(-not (Test-Path -LiteralPath (Join-Path $pythonDirectory 'python.exe'))) {
    Expand-Archive -LiteralPath $pythonArchive -DestinationPath $pythonDirectory -Force
}
$python=Join-Path $pythonDirectory 'python.exe'
$cache=Join-Path $DataDirectory 'assets'
$work=Join-Path $DataDirectory 'preparation'
Write-Host 'Preparing previews and game translations. First installation can take several minutes.'
$modArguments=@()
foreach($archive in $ModArchive) {
    if(-not (Test-Path -LiteralPath $archive)) { throw "Mod archive not found: $archive" }
    $modArguments+=@('--mod',[IO.Path]::GetFullPath($archive))
}
& $python (Join-Path $PSScriptRoot 'prepare-game-assets.py') --game $GameDirectory --converter $converter --work $work --output $cache --workers ([Math]::Max(1,$Workers)) @modArguments
if($LASTEXITCODE -ne 0) { throw 'Game asset preparation failed. The DLL has not been replaced.' }
$sourceDll=Join-Path $packageRoot 'bin\JobDispatcherSP.dll'
if(-not (Test-Path -LiteralPath $sourceDll)) { throw 'Plugin DLL missing. Download the Windows-x64 release archive, or build the source first.' }
Get-ChildItem -LiteralPath (Join-Path $packageRoot 'assets') -Filter 'ui-labels.*.tsv' -File | Copy-Item -Destination $cache -Force
$pluginDirectory=Join-Path $GameDirectory 'bin\win_x64\plugins'
New-Item -ItemType Directory -Path $pluginDirectory -Force | Out-Null
$targetDll=Join-Path $pluginDirectory 'JobDispatcherSP.dll'
if(Test-Path -LiteralPath $targetDll) {
    $backupDirectory=Join-Path $DataDirectory 'backups'
    New-Item -ItemType Directory -Path $backupDirectory -Force | Out-Null
    Copy-Item -LiteralPath $targetDll -Destination (Join-Path $backupDirectory ('JobDispatcherSP-'+[DateTime]::UtcNow.ToString('yyyyMMdd-HHmmssfff')+'.dll'))
}
Copy-Item -LiteralPath $sourceDll -Destination $targetDll -Force
[IO.File]::WriteAllText((Join-Path $pluginDirectory 'JobDispatcherSP.ini'),"[JobDispatcherSP]`r`nDataDirectory=$cache`r`n",[Text.Encoding]::Unicode)
@{version='0.1';game=$GameDirectory;dll=$targetDll;data=$cache;sha256=(Get-FileHash -LiteralPath $targetDll).Hash} | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $DataDirectory 'installation.json') -Encoding UTF8
Write-Host "Installed: $targetDll"
Write-Host "Cache: $cache"
Write-Host 'Start ETS2 using DirectX 11, enter the driving view, and right-click to use the dispatcher icon.'
