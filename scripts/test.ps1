param(
    [string]$QtRoot = 'C:\Qt\6.8.3\mingw_64',
    [string]$MingwRoot = 'C:\Qt\Tools\mingw1310_64',
    [string]$BuildDirectory = 'build\release',
    [ValidateSet('all','network','session','ui','workflow','workflow_ui','workflow_protocol','workflow_integration','workflow_e2e','protocol_debug')][string]$Suite = 'all'
)
$ErrorActionPreference = 'Stop'
$projectRoot = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$buildRoot = if ([IO.Path]::IsPathRooted($BuildDirectory)) { $BuildDirectory } else { Join-Path $projectRoot $BuildDirectory }
$serialBin = Join-Path $projectRoot '.deps\qtserialport-install\bin'
$env:Path = "$MingwRoot\bin;$QtRoot\bin;$serialBin;$env:Path"
$arguments = @('--test-dir',$buildRoot,'--output-on-failure')
if ($Suite -ne 'all') { $arguments += @('-R',"^$Suite`$") }
& ctest @arguments
if ($LASTEXITCODE -ne 0) { throw "CTest failed with code $LASTEXITCODE; inspect $buildRoot\test-reports and Testing\Temporary\LastTest.log" }
