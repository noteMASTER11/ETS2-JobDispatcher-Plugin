param([Parameter(Mandatory)][string]$Fixture)
$ErrorActionPreference='Stop'
. (Join-Path $PSScriptRoot '..\tools\install-settings.ps1')
function Check($Value,[string]$Message){if(-not $Value){throw $Message}}
New-Item -ItemType Directory -Path $Fixture -Force|Out-Null
$plugins=Join-Path $Fixture 'plugins'
New-Item -ItemType Directory -Path $plugins -Force|Out-Null
$custom=Join-Path $Fixture 'custom data'
$default=Join-Path $plugins 'JobDispatcherSP\data'
$ini=Join-Path $plugins 'JobDispatcherSP.ini'
Remove-Item -LiteralPath $ini -Force -ErrorAction SilentlyContinue
Check ((Resolve-DispatcherDataDirectory $plugins '') -eq $default) 'Fresh install must use the bundled data directory'
[IO.File]::WriteAllText($ini,"[JobDispatcherSP]`r`nDataDirectory=auto`r`n",[Text.Encoding]::Unicode)
Check ((Resolve-DispatcherDataDirectory $plugins '') -eq $default) 'Portable auto directory must resolve relative to the plugin'
[IO.File]::WriteAllText($ini,"[JobDispatcherSP]`r`nDataDirectory=$custom`r`n",[Text.Encoding]::Unicode)
Check ((Resolve-DispatcherDataDirectory $plugins '') -eq $custom) 'Update lost the existing data directory'
Check ((Resolve-DispatcherDataDirectory $plugins $default) -eq $default) 'Explicit data override was ignored'
New-Item -ItemType Directory -Path $custom -Force|Out-Null
$settings=Join-Path $custom 'offline-settings.json'
@{workers=7;extra_mods=@('D:\ChatGPT\Temp\custom.scs');future_option='keep'}|ConvertTo-Json|Set-Content -LiteralPath $settings -Encoding utf8
$original=[IO.File]::ReadAllText($settings)
Save-DispatcherSettings $custom @{}
Check ([IO.File]::ReadAllText($settings) -eq $original) 'An update without overrides rewrote settings'
Save-DispatcherSettings $custom @{Workers=2}
$result=Get-Content -LiteralPath $settings -Raw|ConvertFrom-Json
Check ($result.workers -eq 2 -and $result.extra_mods[0] -eq 'D:\ChatGPT\Temp\custom.scs') 'Worker override lost the mod list'
Check ($result.future_option -eq 'keep') 'Update removed an unrecognized setting'
Save-DispatcherSettings $custom @{ModArchive=@()}
$result=Get-Content -LiteralPath $settings -Raw|ConvertFrom-Json
Check ($result.extra_mods.Count -eq 0 -and $result.workers -eq 2) 'Explicit empty mod override did not clear the list'
function Invoke-InstallSettings {
 [CmdletBinding()]param([int]$Workers)
 Save-DispatcherSettings $custom $PSBoundParameters
}
Invoke-InstallSettings -Workers 3
$result=Get-Content -LiteralPath $settings -Raw|ConvertFrom-Json
Check ($result.workers -eq 3) 'Installer bound parameters were not accepted'
Write-Output 'Install/update settings, explicit overrides and idempotency passed'
