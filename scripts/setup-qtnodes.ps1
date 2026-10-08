param()
$ErrorActionPreference = 'Stop'
$projectRoot = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$commit = '7c6341a66a8e46b8988140b9e60d892b6a3560b3'
$archiveHash = 'c76deb928e4244654c3e620c2a1160f576fa814a17f5c23387a22886df00bf8d'
$depsRoot = Join-Path $projectRoot '.deps\workflow-research'
$sourceRoot = Join-Path $depsRoot "nodeeditor-$commit"
$archive = Join-Path $depsRoot 'pinned-qtnodes.zip'
New-Item -ItemType Directory -Force $depsRoot | Out-Null
if (!(Test-Path $archive)) {
    Invoke-WebRequest -Uri "https://codeload.github.com/paceholder/nodeeditor/zip/$commit" -OutFile $archive -UseBasicParsing
}
if ((Get-FileHash -LiteralPath $archive -Algorithm SHA256).Hash.ToLowerInvariant() -ne $archiveHash) {
    throw 'QtNodes archive SHA256 mismatch; preserved file for investigation.'
}
if (!(Test-Path (Join-Path $sourceRoot 'CMakeLists.txt'))) {
    Expand-Archive -LiteralPath $archive -DestinationPath $depsRoot -Force
}
& python (Join-Path $PSScriptRoot 'patch-qtnodes.py') $sourceRoot
if ($LASTEXITCODE -ne 0) { throw 'QtNodes source patch failed; original files preserved.' }
Write-Host "Pinned QtNodes 3.0.16 ready: $sourceRoot"
