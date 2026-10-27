[CmdletBinding()]
param([string]$Repository='noteMASTER11/ETS2-JobDispatcher-Plugin')
$ErrorActionPreference='Stop'
$root=Split-Path $PSScriptRoot -Parent
$expected='noteMASTER11/ETS2-JobDispatcher-Plugin'
if($Repository -ne $expected) { throw "Unexpected destination repository: $Repository" }
$gh=Get-Command gh.exe -ErrorAction SilentlyContinue
if(-not $gh -and (Test-Path 'D:\ChatGPT\Tools\GitHubCLI\bin\gh.exe')) { $gh=Get-Item 'D:\ChatGPT\Tools\GitHubCLI\bin\gh.exe' }
if(-not $gh) { throw 'Install GitHub CLI, then run gh auth login.' }
$ghPath=if($gh.Source){$gh.Source}else{$gh.FullName}
& $ghPath auth status
if($LASTEXITCODE) { throw 'GitHub CLI is not signed in. Run gh auth login, then repeat this script.' }
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
