[CmdletBinding()]
param([string]$DependencyRoot = (Join-Path (Split-Path $PSScriptRoot -Parent) 'build\dependencies'))
$ErrorActionPreference = 'Stop'
New-Item -ItemType Directory -Path $DependencyRoot -Force | Out-Null
$env:TEMP = Join-Path $DependencyRoot 'temp'
$env:TMP = $env:TEMP
New-Item -ItemType Directory -Path $env:TEMP -Force | Out-Null
$items = @(
    @{ Name='imgui-v1.93.0.zip'; Folder='imgui-1.93.0'; Uri='https://codeload.github.com/ocornut/imgui/zip/refs/tags/v1.93.0'; Hash='E21907FD8DD5EA59AA00538632A02AD0A3EDEDB4DDC533B94702331FBCD6C470' },
    @{ Name='minhook-v1.3.4.zip'; Folder='minhook-1.3.4'; Uri='https://codeload.github.com/TsudaKageyu/minhook/zip/refs/tags/v1.3.4'; Hash='172708123DAA0C98D20D3A980B16A50BE14AF243DC95DEE6F79C24193AD010E4' }
)
foreach ($item in $items) {
    $archivePath = Join-Path $DependencyRoot $item.Name
    if (!(Test-Path -LiteralPath $archivePath)) {
        Invoke-WebRequest -Uri $item.Uri -OutFile $archivePath
    }
    if ((Get-FileHash -LiteralPath $archivePath -Algorithm SHA256).Hash -ne $item.Hash) {
        throw "Dependency archive checksum mismatch: $archivePath"
    }
    if (!(Test-Path -LiteralPath (Join-Path $DependencyRoot $item.Folder))) {
        Expand-Archive -LiteralPath $archivePath -DestinationPath $DependencyRoot
    }
    Write-Output "$($item.Folder): verified"
}
