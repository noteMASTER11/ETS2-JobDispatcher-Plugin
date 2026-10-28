function Get-DispatcherReleaseVersion([string]$Root) {
 $version=[IO.File]::ReadAllText((Join-Path $Root 'VERSION')).Trim()
 if($version -notmatch '^\d+\.\d+$'){throw "Invalid release version: $version"}
 return $version
}
