param(
    [string]$QtRoot = 'C:\Qt\6.8.3\mingw_64',
    [string]$MingwRoot = 'C:\Qt\Tools\mingw1310_64',
    [ValidateRange(1,16)][int]$Parallel = 4
)
$ErrorActionPreference = 'Stop'
$projectRoot = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$depsRoot = Join-Path $projectRoot '.deps\workflow'
$downloads = Join-Path $depsRoot 'downloads'
New-Item -ItemType Directory -Force $downloads | Out-Null
function Invoke-Native([string]$Command, [string[]]$Arguments) {
    & $Command @Arguments
    if ($LASTEXITCODE -ne 0) { throw "$Command exited with code $LASTEXITCODE" }
}
function Archive([string]$Name, [string]$Url, [string]$Hash, [string]$Source) {
    $path = Join-Path $downloads $Name
    if (!(Test-Path $path)) { Invoke-WebRequest -Uri $Url -OutFile $path -UseBasicParsing }
    if ((Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash.ToLowerInvariant() -ne $Hash) { throw "Dependency SHA256 mismatch: $Name" }
    if (!(Test-Path (Join-Path $depsRoot $Source))) { Invoke-Native 'tar.exe' @('-xf',$path,'-C',$depsRoot) }
}
Archive 'cpr-1.14.2.tar.gz' 'https://codeload.github.com/libcpr/cpr/tar.gz/refs/tags/1.14.2' 'b9b529b47083bfe80bba855ca5308d12d767ae7c7b629aef5ef018c4343cf62b' 'cpr-1.14.2\CMakeLists.txt'
Archive 'curl-8.16.0.tar.xz' 'https://curl.se/download/curl-8.16.0.tar.xz' '40c8cddbcb6cc6251c03dea423a472a6cea4037be654ba5cf5dec6eb2d22ff1d' 'curl-8.16.0\CMakeLists.txt'
Archive 'c-ares-1.34.5.tar.gz' 'https://github.com/c-ares/c-ares/releases/download/v1.34.5/c-ares-1.34.5.tar.gz' '7d935790e9af081c25c495fd13c2cfcda4792983418e96358ef6e7320ee06346' 'c-ares-1.34.5\CMakeLists.txt'
Archive 'boost_1_89_0.zip' 'https://archives.boost.io/release/1.89.0/source/boost_1_89_0.zip' '77bee48e32cabab96a3fd2589ec3ab9a17798d330220fdd8bde6ff5611b4ccde' 'boost_1_89_0\boost\beast.hpp'
Archive 'openssl-3.5.4.tar.gz' 'https://github.com/openssl/openssl/releases/download/openssl-3.5.4/openssl-3.5.4.tar.gz' '967311f84955316969bdb1d8d4b983718ef42338639c621ec4c34fddef355e99' 'openssl-3.5.4\Configure'
# Git's MSYS Perl is required by OpenSSL's mingw64 Unix makefile generator.
# Its minimal installation lacks these pure-Perl modules. No CPAN install and
# no system mutation: use pinned build-only source modules in .deps.
Archive 'Locale-Maketext-Simple-0.21.tar.gz' 'https://cpan.metacpan.org/authors/id/J/JE/JESSE/Locale-Maketext-Simple-0.21.tar.gz' 'b009ff51f4fb108d19961a523e99b4373ccf958d37ca35bf1583215908dca9a9' 'Locale-Maketext-Simple-0.21\lib\Locale\Maketext\Simple.pm'
Archive 'ExtUtils-MakeMaker-7.76.tar.gz' 'https://cpan.metacpan.org/authors/id/B/BI/BINGOS/ExtUtils-MakeMaker-7.76.tar.gz' '30bcfd75fec4d512e9081c792f7cb590009d9de2fe285ffa8eec1be35a5ae7ca' 'ExtUtils-MakeMaker-7.76\lib\ExtUtils\MakeMaker.pm'
$perlArchive = Join-Path $downloads 'strawberry-perl-5.40.4.1-64bit-portable.zip'
if (!(Test-Path $perlArchive)) { Invoke-WebRequest -UseBasicParsing -Uri 'https://github.com/StrawberryPerl/Perl-Dist-Strawberry/releases/download/SP_54041_64bit/strawberry-perl-5.40.4.1-64bit-portable.zip' -OutFile $perlArchive }
if ((Get-FileHash $perlArchive -Algorithm SHA256).Hash.ToLowerInvariant() -ne 'aa9052ac082c8a8a0f952823b3f4f0bf9c1d0f84bbe0b3dfe0aa94291d6b1045') { throw 'Strawberry build module archive SHA256 mismatch' }
$pod = Join-Path $depsRoot 'perl-support\lib\Pod'
if (!(Test-Path (Join-Path $pod 'Simple.pm'))) {
    $support = Join-Path $depsRoot 'perl-support'
    New-Item -ItemType Directory -Force $support | Out-Null
    Invoke-Native 'tar.exe' @('-xf',$perlArchive,'-C',$support,'perl/lib/Pod')
    New-Item -ItemType Directory -Force (Join-Path $support 'lib') | Out-Null
    Move-Item (Join-Path $support 'perl\lib\Pod') $pod
}
$git = (Get-Command git.exe).Source
$gitRoot = Split-Path (Split-Path $git -Parent) -Parent
if ((Split-Path $gitRoot -Leaf) -eq 'mingw64') { $gitRoot = Split-Path $gitRoot -Parent }
$bash = Join-Path $gitRoot 'bin\bash.exe'
if (!(Test-Path $bash)) { throw 'Git for Windows bash and MSYS Perl are required for the OpenSSL mingw64 build' }
if (!(Test-Path "$MingwRoot\bin\g++.exe")) { throw 'Matching MinGW toolchain is missing' }
$compilerVersion = (& "$MingwRoot\bin\g++.exe" -dumpfullversion).Trim()
$compilerMachine = (& "$MingwRoot\bin\g++.exe" -dumpmachine).Trim()
if ($compilerVersion -ne '13.1.0' -or $compilerMachine -ne 'x86_64-w64-mingw32') { throw 'Workflow dependencies require Qt MinGW GCC 13.1.0 x86_64' }
if (!(Test-Path "$QtRoot\bin\qmake.exe") -or (& "$QtRoot\bin\qmake.exe" -query QT_VERSION).Trim() -ne '6.8.3') { throw 'Workflow build requires Qt 6.8.3' }
$build = Join-Path $projectRoot 'build\workflow-protocol\openssl'
$install = Join-Path $depsRoot 'openssl-install'
$stamp = Join-Path $install 'portbridge-build.json'
$compilerHash = (Get-FileHash "$MingwRoot\bin\gcc.exe" -Algorithm SHA256).Hash
$locked = [ordered]@{ openssl = '3.5.4'; compilerSHA256 = $compilerHash; options = 'mingw64 no-shared no-tests no-apps no-docs no-module'; sourceDateEpoch = 1759190400; sourceSHA256 = '967311f84955316969bdb1d8d4b983718ef42338639c621ec4c34fddef355e99' }
$stampText = $locked | ConvertTo-Json
if (!(Test-Path $stamp) -or (Get-Content -Raw $stamp).Trim() -ne $stampText.Trim()) {
    New-Item -ItemType Directory -Force $build | Out-Null
    # Shell parameters are positional, never interpolated into shell code.
    $shell = @'
set -eu
export PATH="$(cygpath -u "$1")/bin:$PATH"
deps=$(cygpath -u "$2")
export PERL5LIB="$deps/Locale-Maketext-Simple-0.21/lib:$deps/ExtUtils-MakeMaker-7.76/lib:$deps/perl-support/lib"
export SOURCE_DATE_EPOCH=1759190400
cd "$(cygpath -u "$3")"
# Force build information to use the locked release epoch, even when upgrading
# an existing local build produced before this reproducibility setting.
rm -f crypto/buildinf.h
perl ../../../.deps/workflow/openssl-3.5.4/Configure mingw64 no-shared no-tests no-apps no-docs no-module "--prefix=$4"
mingw32-make -j"$5"
mingw32-make install_sw
'@
    $shellPath = Join-Path $build 'setup-openssl.sh'
    [IO.File]::WriteAllText($shellPath, $shell.Replace("`r`n", "`n"), (New-Object Text.UTF8Encoding($false)))
    Invoke-Native $bash @($shellPath,$MingwRoot,$depsRoot,$build,($install.Replace('\','/')),"$Parallel")
    $stampText | Set-Content -Encoding UTF8 $stamp
}
$licenses = Join-Path $depsRoot 'licenses'
New-Item -ItemType Directory -Force $licenses | Out-Null
Copy-Item (Join-Path $depsRoot 'cpr-1.14.2\LICENSE') (Join-Path $licenses 'cpr-MIT.txt')
Copy-Item (Join-Path $depsRoot 'cpr-1.14.2\test\LICENSE') (Join-Path $licenses 'cpr-test-license.txt')
Copy-Item (Join-Path $depsRoot 'curl-8.16.0\COPYING') (Join-Path $licenses 'curl.txt')
Copy-Item (Join-Path $depsRoot 'c-ares-1.34.5\LICENSE.md') (Join-Path $licenses 'c-ares-MIT.txt')
Copy-Item (Join-Path $depsRoot 'boost_1_89_0\LICENSE_1_0.txt') (Join-Path $licenses 'Boost-BSL-1.0.txt')
Copy-Item (Join-Path $depsRoot 'openssl-3.5.4\LICENSE.txt') (Join-Path $licenses 'OpenSSL-Apache-2.0.txt')
# These static libraries introduce no new deployment DLLs. The application
# still needs Qt6Core and the matching MinGW compiler runtime from windeployqt.
$artifacts = @("$install\lib64\libssl.a", "$install\lib64\libcrypto.a", "$MingwRoot\bin\libgcc_s_seh-1.dll", "$MingwRoot\bin\libstdc++-6.dll", "$MingwRoot\bin\libwinpthread-1.dll")
$manifest = @($artifacts | ForEach-Object { @{path=$_; sha256=(Get-FileHash $_ -Algorithm SHA256).Hash.ToLowerInvariant()} })
@{ staticDependencies=@('cpr 1.14.2','libcurl 8.16.0','c-ares 1.34.5','Boost 1.89.0','OpenSSL 3.5.4'); artifacts=$manifest; licenses=$licenses } | ConvertTo-Json -Depth 5 | Set-Content -Encoding UTF8 (Join-Path $depsRoot 'deployment-manifest.json')
Write-Host "Pinned protocol sources/static TLS ready: $depsRoot"
