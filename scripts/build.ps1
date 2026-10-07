param(
    [string]$QtRoot = 'C:\Qt\6.8.3\mingw_64',
    [string]$MingwRoot = 'C:\Qt\Tools\mingw1310_64',
    [string]$NinjaRoot = 'C:\Qt\Tools\Ninja',
    [ValidateSet('Release','Debug')][string]$Configuration = 'Release',
    [ValidateRange(1,32)][int]$Parallel = 4,
    [switch]$SkipTests
)
$ErrorActionPreference = 'Stop'
$projectRoot = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$depsRoot = Join-Path $projectRoot '.deps'
$serialInstall = Join-Path $depsRoot 'qtserialport-install'
$env:Path = "$MingwRoot\bin;$NinjaRoot;$QtRoot\bin;$serialInstall\bin;$env:Path"
function Invoke-Native {
    param([string]$Command, [string[]]$Arguments)
    & $Command @Arguments
    if ($LASTEXITCODE -ne 0) { throw "$Command exited with code $LASTEXITCODE" }
}
function Assert-DependencyCommit([string]$Directory, [string]$Expected) {
    $commit = (& git -C $Directory rev-parse HEAD)
    if ($LASTEXITCODE -ne 0 -or $commit.Trim() -ne $Expected) { throw "Dependency commit mismatch at $Directory; expected $Expected" }
}
if (!(Test-Path "$QtRoot\lib\cmake\Qt6\Qt6Config.cmake")) { throw "Qt 6 SDK not found: $QtRoot" }
if (!(Test-Path "$MingwRoot\bin\g++.exe")) { throw "Matching MinGW toolchain not found: $MingwRoot" }
if (!(Test-Path "$NinjaRoot\ninja.exe")) { throw "Ninja not found: $NinjaRoot" }
New-Item -ItemType Directory -Force $depsRoot | Out-Null
$asioSource = Join-Path $depsRoot 'asio'
if (!(Test-Path "$asioSource\asio\include\asio.hpp")) {
    Invoke-Native 'git' @('clone','--depth','1','--branch','asio-1-30-2','https://github.com/chriskohlhoff/asio.git',$asioSource)
}
Assert-DependencyCommit $asioSource '12e0ce9e0500bf0f247dbd1ae894272656456079'
if (!(Test-Path "$QtRoot\lib\cmake\Qt6SerialPort\Qt6SerialPortConfig.cmake") -and !(Test-Path "$serialInstall\lib\cmake\Qt6SerialPort\Qt6SerialPortConfig.cmake")) {
    $serialSource = Join-Path $depsRoot 'qtserialport'
    if (!(Test-Path "$serialSource\CMakeLists.txt")) {
        Invoke-Native 'git' @('clone','--depth','1','--branch','v6.8.3','https://github.com/qt/qtserialport.git',$serialSource)
    }
    Assert-DependencyCommit $serialSource '704c70cde4d3153d2871e39d5bc10ccbd270f850'
    $serialBuild = Join-Path $projectRoot 'build\qtserialport'
    Invoke-Native 'cmake' @('-S',$serialSource,'-B',$serialBuild,'-G','Ninja','-DCMAKE_BUILD_TYPE=Release',"-DCMAKE_PREFIX_PATH=$QtRoot","-DCMAKE_INSTALL_PREFIX=$serialInstall",'-DQT_BUILD_TESTS=OFF','-DQT_BUILD_EXAMPLES=OFF')
    Invoke-Native 'cmake' @('--build',$serialBuild,'--parallel',"$Parallel")
    Invoke-Native 'cmake' @('--install',$serialBuild)
}
$serialConfigRoot = if (Test-Path "$QtRoot\lib\cmake\Qt6SerialPort\Qt6SerialPortConfig.cmake") { "$QtRoot\lib\cmake\Qt6SerialPort" } else { "$serialInstall\lib\cmake\Qt6SerialPort" }
$buildRoot = Join-Path $projectRoot ("build\" + $Configuration.ToLowerInvariant())
Invoke-Native 'cmake' @('-S',$projectRoot,'-B',$buildRoot,'-G','Ninja',"-DCMAKE_BUILD_TYPE=$Configuration","-DCMAKE_PREFIX_PATH=$QtRoot;$serialInstall","-DQt6SerialPort_DIR=$serialConfigRoot","-DCMAKE_CXX_COMPILER=$MingwRoot\bin\g++.exe",'-DBUILD_TESTING=ON')
Invoke-Native 'cmake' @('--build',$buildRoot,'--parallel',"$Parallel")
if (!$SkipTests) { Invoke-Native 'ctest' @('--test-dir',$buildRoot,'--output-on-failure') }
Write-Host "Built: $buildRoot\PortBridge.exe"
