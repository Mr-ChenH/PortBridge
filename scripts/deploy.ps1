param(
    [string]$QtRoot = 'C:\Qt\6.8.3\mingw_64',
    [string]$MingwRoot = 'C:\Qt\Tools\mingw1310_64',
    [string]$BuildDirectory = 'build\release',
    [string]$OutputDirectory = 'dist\PortBridge'
)
$ErrorActionPreference = 'Stop'
$projectRoot = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$buildRoot = if ([IO.Path]::IsPathRooted($BuildDirectory)) { $BuildDirectory } else { Join-Path $projectRoot $BuildDirectory }
$distRoot = if ([IO.Path]::IsPathRooted($OutputDirectory)) { $OutputDirectory } else { Join-Path $projectRoot $OutputDirectory }
$serialInstall = Join-Path $projectRoot '.deps\qtserialport-install'
$env:Path = "$MingwRoot\bin;$QtRoot\bin;$serialInstall\bin;$env:Path"
if (!(Test-Path "$buildRoot\PortBridge.exe")) { throw 'Build PortBridge first using scripts/build.ps1.' }
New-Item -ItemType Directory -Force $distRoot | Out-Null
Copy-Item "$buildRoot\PortBridge.exe" $distRoot -Force
if (Test-Path "$serialInstall\bin\Qt6SerialPort.dll") { Copy-Item "$serialInstall\bin\Qt6SerialPort.dll" $distRoot -Force }
$qtPaths = Join-Path $QtRoot 'bin\qtpaths6.exe'
if (!(Test-Path "$QtRoot\bin\Qt6SerialPort.dll")) {
    if (!(Test-Path "$serialInstall\bin\Qt6SerialPort.dll")) { throw 'Matching Qt SerialPort runtime is missing.' }
    # windeployqt resolves every Qt module from QT_INSTALL_BINS. Build a local
    # read-only input overlay so separately installed modules are discoverable.
    $deployBin = Join-Path $projectRoot '.deps\qt-deploy-sdk\bin'
    New-Item -ItemType Directory -Force $deployBin | Out-Null
    function Add-DeploymentInput([string]$Source) {
        $target = Join-Path $deployBin ([IO.Path]::GetFileName($Source))
        if (Test-Path $target) { Remove-Item $target -Force }
        try { New-Item -ItemType HardLink -Path $target -Target $Source -ErrorAction Stop | Out-Null }
        catch { Copy-Item $Source $target -Force }
    }
    Get-ChildItem "$QtRoot\bin" -Filter '*.dll' | ForEach-Object { Add-DeploymentInput $_.FullName }
    Add-DeploymentInput "$serialInstall\bin\Qt6SerialPort.dll"
    Copy-Item $qtPaths "$deployBin\qtpaths6.exe" -Force
    $deploySdkRoot = Split-Path $deployBin -Parent
    $deployModules = Join-Path $deploySdkRoot 'modules'
    New-Item -ItemType Directory -Force $deployModules | Out-Null
    Copy-Item "$QtRoot\modules\*.json" $deployModules -Force
    Copy-Item "$serialInstall\modules\SerialPort.json" $deployModules -Force
    $qtConfig = "[Paths]`nPrefix=$($QtRoot.Replace('\','/'))`nBinaries=$($deployBin.Replace('\','/'))`nArchData=$($deploySdkRoot.Replace('\','/'))`n"
    [IO.File]::WriteAllText((Join-Path $deployBin 'qt.conf'), $qtConfig, [Text.UTF8Encoding]::new($false))
    $qtPaths = Join-Path $deployBin 'qtpaths6.exe'
}
& "$QtRoot\bin\windeployqt.exe" --qtpaths $qtPaths --release --compiler-runtime --no-translations "$distRoot\PortBridge.exe"
if ($LASTEXITCODE -ne 0) { throw "windeployqt exited with code $LASTEXITCODE" }
[IO.File]::WriteAllText((Join-Path $distRoot 'qt.conf'), "[Paths]`nPrefix=.`nPlugins=.`n", [Text.UTF8Encoding]::new($false))
if (Test-Path "$buildRoot\portbridge_bench.exe") { Copy-Item "$buildRoot\portbridge_bench.exe" $distRoot -Force }
Copy-Item (Join-Path $projectRoot 'README.md') $distRoot -Force
Copy-Item (Join-Path $projectRoot 'THIRD_PARTY_NOTICES.md') $distRoot -Force
$iconSource = Join-Path $projectRoot 'assets\app-icon'
if (Test-Path $iconSource) {
    $iconTarget = Join-Path $distRoot 'assets\app-icon'
    New-Item -ItemType Directory -Force $iconTarget | Out-Null
    foreach ($file in @('portbridge.svg', 'portbridge.png', 'portbridge.ico', 'preview.png')) {
        Copy-Item (Join-Path $iconSource $file) $iconTarget -Force
    }
}
$sourceDocs = Join-Path $projectRoot 'docs'
if (Test-Path $sourceDocs) {
    $packagedDocs = Join-Path $distRoot 'docs'
    Get-ChildItem $sourceDocs -File -Recurse | ForEach-Object {
        $relativeFile = $_.FullName.Substring($sourceDocs.Length + 1)
        $packagedFile = Join-Path $packagedDocs $relativeFile
        New-Item -ItemType Directory -Force (Split-Path $packagedFile -Parent) | Out-Null
        Copy-Item -LiteralPath $_.FullName -Destination $packagedFile -Force
    }
}
$licenseRoot = Join-Path $distRoot 'third-party-licenses'
New-Item -ItemType Directory -Force $licenseRoot | Out-Null
if (Test-Path "$projectRoot\.deps\asio\asio\LICENSE_1_0.txt") { Copy-Item "$projectRoot\.deps\asio\asio\LICENSE_1_0.txt" "$licenseRoot\Asio-Boost-Software-License.txt" -Force }
if (Test-Path "$projectRoot\.deps\qtserialport\LICENSES") {
    $serialLicenses = Join-Path $licenseRoot 'QtSerialPort'
    New-Item -ItemType Directory -Force $serialLicenses | Out-Null
    Copy-Item "$projectRoot\.deps\qtserialport\LICENSES\*" $serialLicenses -Recurse -Force
    Copy-Item "$projectRoot\.deps\qtserialport\LICENSES\LGPL-3.0-only.txt" "$licenseRoot\Qt-LGPL-3.0.txt" -Force
}
$workflowLicenseRoot = Join-Path $projectRoot '.deps\workflow\licenses'
if (!(Test-Path $workflowLicenseRoot)) { throw 'Workflow dependency licenses missing; run setup-workflow-deps.ps1.' }
Copy-Item "$workflowLicenseRoot\*" $licenseRoot -Force
$qtNodesLicense = Join-Path $projectRoot '.deps\workflow-research\nodeeditor-7c6341a66a8e46b8988140b9e60d892b6a3560b3\LICENSE.rst'
Copy-Item $qtNodesLicense (Join-Path $licenseRoot 'QtNodes-BSD-3-Clause.txt') -Force
Copy-Item (Join-Path $projectRoot 'patches\qtnodes-qvariant-const.patch') (Join-Path $licenseRoot 'QtNodes-PortBridge.patch') -Force
$protocolDeployment = Get-Content -Raw (Join-Path $projectRoot '.deps\workflow\deployment-manifest.json') | ConvertFrom-Json
$publicDependencies = [ordered]@{
    staticDependencies = $protocolDeployment.staticDependencies
    artifacts = @($protocolDeployment.artifacts | ForEach-Object { @{name=[IO.Path]::GetFileName($_.path);sha256=$_.sha256} })
}
[IO.File]::WriteAllText((Join-Path $licenseRoot 'protocol-dependencies.json'), ($publicDependencies | ConvertTo-Json -Depth 5), [Text.UTF8Encoding]::new($false))
foreach ($component in @('gcc', 'winpthreads', 'mingw-w64')) {
    $componentSource = Join-Path $MingwRoot "licenses\$component"
    if (Test-Path $componentSource) {
        $componentTarget = Join-Path $licenseRoot "MinGW\$component"
        New-Item -ItemType Directory -Force $componentTarget | Out-Null
        Copy-Item "$componentSource\*" $componentTarget -Recurse -Force
    }
}
Write-Host "Deployment ready: $distRoot\PortBridge.exe"
