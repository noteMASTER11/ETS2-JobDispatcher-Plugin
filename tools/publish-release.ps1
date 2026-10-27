[CmdletBinding()]
param([string]$Repository='noteMASTER11/ETS2-JobDispatcher-Plugin', [string]$GitHubCli='gh')
$ErrorActionPreference='Stop'
$root=Split-Path $PSScriptRoot -Parent
$expected='noteMASTER11/ETS2-JobDispatcher-Plugin'
if($Repository -ne $expected) { throw "Unexpected destination repository: $Repository" }
$gh=Get-Command $GitHubCli -ErrorAction SilentlyContinue
if(-not $gh) { throw 'Install GitHub CLI, then run gh auth login.' }
$ghPath=$gh.Source
$login=& $ghPath api user --jq .login
if($LASTEXITCODE) { throw 'GitHub CLI could not verify the account. Run gh auth login or retry after a network error.' }
if($login -ne 'noteMASTER11') { throw "Unexpected GitHub account: $login" }
Push-Location $root
try {
    git diff --exit-code
    if($LASTEXITCODE) { throw 'Commit local changes before publishing.' }
    foreach($name in @('JobDispatcherSP-0.1-Windows-x64.zip','ETS2-JobDispatcher-Plugin-0.1-source.zip','SHA256SUMS.txt')) {
        if(-not (Test-Path -LiteralPath (Join-Path 'dist' $name))) { throw "Release file missing: $name" }
    }
    git push origin main
    if($LASTEXITCODE) { throw 'Git push failed.' }
    $revision=git rev-parse HEAD
    & $ghPath release create v0.1 'dist/JobDispatcherSP-0.1-Windows-x64.zip' 'dist/ETS2-JobDispatcher-Plugin-0.1-source.zip' 'dist/SHA256SUMS.txt' --repo $Repository --target $revision --title 'Job Dispatcher SP 0.1' --notes-file docs/RELEASE-0.1.md
    if($LASTEXITCODE) { throw 'GitHub release creation failed.' }
} finally { Pop-Location }
