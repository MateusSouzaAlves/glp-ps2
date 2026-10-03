[CmdletBinding()]
param(
    [Parameter(Mandatory=$true)][string]$SourceDir,
    [Parameter(Mandatory=$true)][string]$DependencyDir,
    [Parameter(Mandatory=$true)][string]$BuildDir,
    [Parameter(Mandatory=$true)][string]$SdkDir,
    [switch]$Compile,
    [ValidateSet('c++_shared','c++_static')][string]$Stl = 'c++_shared',
    [ValidateRange(1,8)][int]$Jobs = 2
)
$ErrorActionPreference = 'Stop'
$project = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path.TrimEnd('\', '/')
$candidate = Get-Content -LiteralPath (Join-Path $project 'config/ps2-armsx2-candidate.json') -Raw | ConvertFrom-Json
$source = [IO.Path]::GetFullPath($SourceDir).TrimEnd('\', '/')
$deps = [IO.Path]::GetFullPath($DependencyDir).TrimEnd('\', '/')
$build = [IO.Path]::GetFullPath($BuildDir).TrimEnd('\', '/')
foreach ($protectedPath in @($project, $source, $deps)) {
    if ($build -eq $protectedPath -or
            $build.StartsWith($protectedPath+'\', [StringComparison]::OrdinalIgnoreCase) -or
            $protectedPath.StartsWith($build+'\', [StringComparison]::OrdinalIgnoreCase)) {
        throw 'Use a separate external build cache, not source/dependencies/project or ancestors.'
    }
}
if ($build -eq [IO.Path]::GetPathRoot($build).TrimEnd('\', '/')) {
    throw 'Refusing a drive-root build.'
}
& (Join-Path $PSScriptRoot 'prepare-armsx2-source.ps1') -SourceDir $source -DependencyDir $deps -VerifyOnly
if (-not $?) { throw 'Candidate source preflight failed.' }
$cmakeRoot = Join-Path $SdkDir "cmake/$($candidate.toolchain_candidate.cmake)/bin"
$cmake = Join-Path $cmakeRoot 'cmake.exe'
$ninja = Join-Path $cmakeRoot 'ninja.exe'
$ndk = Join-Path $SdkDir "ndk/$($candidate.toolchain_candidate.ndk)"
$toolchain = Join-Path $ndk 'build/cmake/android.toolchain.cmake'
foreach ($required in @($cmake, $ninja, $toolchain)) {
    if (-not (Test-Path -LiteralPath $required -PathType Leaf)) { throw "Missing installed tool: $required" }
}
# Only a marked candidate directory may be reused. Never erase/reset any cache.
$marker = Join-Path $build '.emuorbit-armsx2-candidate'
if (Test-Path -LiteralPath $build) {
    if ((Get-Item -LiteralPath $build).Attributes -band [IO.FileAttributes]::ReparsePoint) {
        throw 'Refusing a reparse-point build cache.'
    }
    if (-not (Test-Path -LiteralPath $marker -PathType Leaf)) {
        if (@(Get-ChildItem -LiteralPath $build -Force).Count) { throw 'Refusing an unmarked nonempty build directory.' }
    }
} else { New-Item -ItemType Directory -Path $build | Out-Null }
if (-not (Test-Path -LiteralPath $marker)) {
    New-Item -ItemType File -Path $marker | Out-Null
}
$recipe = Join-Path $PSScriptRoot 'ps2/armsx2/core-candidate'
$argsForCmake = @('-S', $recipe, '-B', $build, '-G', 'Ninja',
    "-DCMAKE_MAKE_PROGRAM=$ninja", "-DCMAKE_TOOLCHAIN_FILE=$toolchain",
    '-DCMAKE_BUILD_TYPE=Release', '-DANDROID_ABI=arm64-v8a',
    "-DANDROID_PLATFORM=android-$($candidate.toolchain_candidate.minimum_api_to_verify)",
    "-DANDROID_STL=$Stl", "-DARMSX2_SOURCE_DIR=$source", "-DARMSX2_DEPENDENCY_DIR=$deps")
& $cmake @argsForCmake
if ($LASTEXITCODE -ne 0) { throw 'Candidate configure failed; product is unchanged.' }
if ($Compile) {
    & $cmake --build $build --target armsx2_candidate armsx2_abi_probe --parallel $Jobs
    if ($LASTEXITCODE -ne 0) { throw 'Candidate compile/link failed; product is unchanged.' }
}
Write-Output 'Private candidate recipe checked. This is not gameplay, a distribution certificate or a product replacement.'
