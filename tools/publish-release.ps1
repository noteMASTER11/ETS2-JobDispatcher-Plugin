[CmdletBinding()]
param([string]$Repository='noteMASTER11/ETS2-JobDispatcher-Plugin', [string]$GitHubCli='gh', [switch]$Draft)
$ErrorActionPreference='Stop'
$root=Split-Path $PSScriptRoot -Parent
. (Join-Path $PSScriptRoot 'release-version.ps1')
$releaseVersion=Get-DispatcherReleaseVersion $root
$tag="v$releaseVersion"
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
    if(git status --porcelain) { throw 'Commit local changes before publishing.' }
    foreach($name in @("JobDispatcherSP-$releaseVersion-Windows-x64.zip","ETS2-JobDispatcher-Plugin-$releaseVersion-source.zip",'SHA256SUMS.txt')) {
        if(-not (Test-Path -LiteralPath (Join-Path 'dist' $name))) { throw "Release file missing: $name" }
    }
    git push origin main
    if($LASTEXITCODE) { throw 'Git push failed.' }
    $revision=git rev-parse HEAD
    git rev-parse --verify "refs/tags/$tag" 2>$null
    if($LASTEXITCODE){git tag -a $tag -m "Job Dispatcher SP $releaseVersion" $revision}
    $tagRevision=git rev-list -n 1 $tag
    if($tagRevision -ne $revision){throw 'The release tag points to another commit.'}
    git push origin "refs/tags/$tag"
    if($LASTEXITCODE){throw 'Tag push failed.'}
    $releaseOptions=@()
    if($Draft){$releaseOptions+='--draft'}
    & $ghPath release create $tag "dist/JobDispatcherSP-$releaseVersion-Windows-x64.zip" "dist/ETS2-JobDispatcher-Plugin-$releaseVersion-source.zip" 'dist/SHA256SUMS.txt' --repo $Repository --verify-tag --title "Job Dispatcher SP $releaseVersion" --notes-file "docs/RELEASE-$releaseVersion.md" @releaseOptions
    if($LASTEXITCODE) { throw 'GitHub release creation failed.' }
} finally { Pop-Location }
