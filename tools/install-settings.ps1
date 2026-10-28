function Resolve-DispatcherDataDirectory([string]$Plugins,[string]$Requested) {
 if($Requested){return [IO.Path]::GetFullPath($Requested)}
 $ini=Join-Path $Plugins 'JobDispatcherSP.ini'
 if(Test-Path -LiteralPath $ini){
  $match=[regex]::Match([IO.File]::ReadAllText($ini),'(?im)^\s*DataDirectory\s*=\s*(.+?)\s*$')
  if($match.Success -and $match.Groups[1].Value -ne 'auto'){return [IO.Path]::GetFullPath($match.Groups[1].Value)}
 }
 return Join-Path $Plugins 'JobDispatcherSP\data'
}

function Save-DispatcherSettings([string]$DataDirectory,[System.Collections.IDictionary]$Overrides) {
 $path=Join-Path $DataDirectory 'offline-settings.json'
 $settings=@{workers=4;extra_mods=@()}
 if(Test-Path -LiteralPath $path){
  if(-not ($Overrides.Keys -contains 'Workers') -and -not ($Overrides.Keys -contains 'ModArchive')){return}
  $existing=Get-Content -LiteralPath $path -Raw|ConvertFrom-Json
  foreach($property in $existing.PSObject.Properties){$settings[$property.Name]=$property.Value}
 }
 if(($Overrides.Keys -contains 'Workers')){$settings.workers=[Math]::Max(1,[int]$Overrides['Workers'])}
 if(($Overrides.Keys -contains 'ModArchive')){$settings.extra_mods=@($Overrides['ModArchive']|ForEach-Object{[IO.Path]::GetFullPath($_)})}
 $temporary=$path+'.tmp'
 $settings|ConvertTo-Json -Depth 16|Set-Content -LiteralPath $temporary -Encoding utf8
 Move-Item -LiteralPath $temporary -Destination $path -Force
}
