[CmdletBinding()]
param([string]$RuntimeDirectory=(Join-Path (Split-Path $PSScriptRoot -Parent) 'runtime'))
$ErrorActionPreference='Stop'
$root=Split-Path $PSScriptRoot -Parent
$downloads=Join-Path $root 'build\downloads'
$env:TEMP=Join-Path $downloads 'temp';$env:TMP=$env:TEMP
New-Item -ItemType Directory -Path $RuntimeDirectory,$downloads,$env:TEMP,(Join-Path $RuntimeDirectory 'sources') -Force | Out-Null
function Fetch([string]$Url,[string]$Path,[string]$Sha) {
    if(-not (Test-Path -LiteralPath $Path)){Invoke-WebRequest $Url -OutFile $Path -UseBasicParsing}
    if((Get-FileHash -LiteralPath $Path).Hash -ne $Sha){throw "Checksum differs: $Path"}
}
$python=Join-Path $downloads 'python-3.13.1-embed-amd64.zip'
Fetch 'https://www.python.org/ftp/python/3.13.1/python-3.13.1-embed-amd64.zip' $python '7B7923FF0183A8B8FCA90F6047184B419B108CB437F75FC1C002F9D2F8BCEC16'
Expand-Archive -LiteralPath $python -DestinationPath (Join-Path $RuntimeDirectory 'python') -Force
$commit='3cd4e73a86d0c6bd28e117664c50a36cabeccf38'
Fetch "https://raw.githubusercontent.com/mwl4/ConverterPIX/$commit/bin/win_x64/converter_pix.exe" (Join-Path $RuntimeDirectory 'converter_pix.exe') '60310EE8E01563145B71A8FB62F63D7A8B03906334E84E1463918F615F192ED2'
Fetch "https://codeload.github.com/mwl4/ConverterPIX/zip/$commit" (Join-Path $RuntimeDirectory 'sources\ConverterPIX-source.zip') '062253941A4486B85AABF8E66732ED717FEBA5D9D5CF07A11B7D6A7B8CD5A075'
Write-Host 'Offline Python, ConverterPIX and corresponding source are ready.'
