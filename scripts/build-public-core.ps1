# SPDX-License-Identifier: MIT
[CmdletBinding()]
param(
    [Parameter(Mandatory)][string]$CacheDir,
    [Parameter(Mandatory)][string]$SdkDir,
    [ValidateRange(1,8)][int]$Jobs=4,
    [switch]$ConfigureOnly
)
$ErrorActionPreference='Stop'
$project=(Resolve-Path (Join-Path $PSScriptRoot '..')).Path
& python (Join-Path $PSScriptRoot 'verify-public-source.py')
if($LASTEXITCODE -ne 0){throw 'Public source provenance failed'}
$cache=[IO.Path]::GetFullPath($CacheDir).TrimEnd('\','/')
if($cache -eq [IO.Path]::GetPathRoot($cache).TrimEnd('\','/') -or
    $cache -eq $project -or $cache.StartsWith($project+'\',[StringComparison]::OrdinalIgnoreCase) -or
    $project.StartsWith($cache+'\',[StringComparison]::OrdinalIgnoreCase)) {throw 'Use a dedicated external cache'}
$pin=Get-Content -LiteralPath (Join-Path $project 'config/ps2-armsx2-candidate.json') -Raw | ConvertFrom-Json
$source=Join-Path $cache 'source'; $deps=Join-Path $cache 'dependencies'
$modules=@(@{Path='sources/armsx2';Target=$source;Origin=$pin.repository;Revision=$pin.revision})+
    @($pin.external_dependencies | ForEach-Object {
        @{Path=('sources/dependencies/'+$_.name);Target=(Join-Path $deps $_.name);Origin=$_.repository;Revision=$_.revision}
    })
foreach($module in $modules){
    $moduleSource=Join-Path $project $module.Path
    $revision=(& git -C $moduleSource rev-parse HEAD).Trim()
    if($LASTEXITCODE -ne 0 -or $revision -ne $module.Revision -or
        @(& git -C $moduleSource status --porcelain --untracked-files=all).Count){throw 'Initialize and verify pinned public submodules first'}
    if(-not (Test-Path -LiteralPath $module.Target)){
        New-Item -ItemType Directory -Path (Split-Path -Parent $module.Target) -Force | Out-Null
        # Upstream resource/license hashes were inventoried on Windows/CRLF.
        # Set this locally on newly created caches, never in the user's global Git config.
        & git clone --config core.longpaths=true --config core.autocrlf=true --no-checkout --no-hardlinks $moduleSource $module.Target
        if($LASTEXITCODE -ne 0){throw 'Public source clone failed'}
        & git -C $module.Target remote set-url origin $module.Origin
        if($LASTEXITCODE -ne 0){throw 'Cannot set inventoried source origin'}
        & git -C $module.Target checkout --detach $module.Revision
        if($LASTEXITCODE -ne 0){throw 'Pinned source checkout failed'}
    }
}
$build=Join-Path $cache 'build-api26-static'
& (Join-Path $PSScriptRoot 'build-armsx2-candidate.ps1') -SourceDir $source -DependencyDir $deps -BuildDir $build -SdkDir $SdkDir -Stl c++_static -Jobs $Jobs
if(-not $?){throw 'Public native configure failed'}
if($ConfigureOnly){Write-Output 'PUBLIC_COMPONENT_CONFIGURE_PASS_NOT_BUILD_OR_LEGAL_VERDICT';return}
$cmake=Join-Path $SdkDir "cmake/$($pin.toolchain_candidate.cmake)/bin/cmake.exe"
& $cmake --build $build --target armsx2_candidate armsx2_product_host --parallel $Jobs
if($LASTEXITCODE -ne 0){throw 'Public native build failed'}
$strip=Join-Path $SdkDir "ndk/$($pin.toolchain_candidate.ndk)/toolchains/llvm/prebuilt/windows-x86_64/bin/llvm-strip.exe"
$dist=Join-Path $project 'dist'; New-Item -ItemType Directory -Path $dist -Force | Out-Null
foreach($pair in @(@('armsx2_libretro.so','libarmsx2_core.so'),@('libemuorbit_armsx2.so','libemuorbit_armsx2.so'))){
    & $strip --strip-unneeded -o (Join-Path $dist $pair[1]) (Join-Path $build $pair[0])
    if($LASTEXITCODE -ne 0){throw 'Public native strip failed'}
}
$manifest=Get-Content -LiteralPath (Join-Path $project 'config/ps2-armsx2-resources.json') -Raw | ConvertFrom-Json
foreach($item in $manifest.files){
    $resourceInput=Join-Path $source $item.source
    if((Get-Item -LiteralPath $resourceInput).Length -ne $item.bytes -or
        (Get-FileHash -LiteralPath $resourceInput -Algorithm SHA256).Hash -ne $item.sha256){throw 'Resource changed'}
    $target=Join-Path $dist ('resources/'+$item.destination)
    New-Item -ItemType Directory -Path (Split-Path -Parent $target) -Force | Out-Null
    Copy-Item -LiteralPath $resourceInput -Destination $target
}
Copy-Item -LiteralPath (Join-Path $project 'LICENSE') -Destination (Join-Path $dist 'COPYING.GPLv3')
Write-Output 'PUBLIC_COMPONENT_BUILD_PASS_NO_BIOS_NO_APK_NOT_LEGAL_VERDICT'
