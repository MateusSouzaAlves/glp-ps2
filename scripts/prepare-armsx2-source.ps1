[CmdletBinding()]
param(
    [Parameter(Mandatory=$true)][string]$SourceDir,
    [string]$DependencyDir,
    [switch]$VerifyOnly
)
$ErrorActionPreference = 'Stop'
$project = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path.TrimEnd('\', '/')
$candidate = Get-Content -LiteralPath (Join-Path $project 'config/ps2-armsx2-candidate.json') -Raw | ConvertFrom-Json
$source = [IO.Path]::GetFullPath($SourceDir).TrimEnd('\', '/')
$root = [IO.Path]::GetPathRoot($source).TrimEnd('\', '/')
if ($source -eq $root -or $source -eq $project -or
        $source.StartsWith($project+'\', [StringComparison]::OrdinalIgnoreCase) -or
        $project.StartsWith($source+'\', [StringComparison]::OrdinalIgnoreCase)) {
    throw 'Use a dedicated external source cache, not the project, ancestor or drive root.'
}
if (Test-Path -LiteralPath $source) {
    if ((Get-Item -LiteralPath $source).Attributes -band [IO.FileAttributes]::ReparsePoint) {
        throw 'Refusing a reparse-point source cache.'
    }
    if (-not (Test-Path -LiteralPath (Join-Path $source '.git'))) {
        throw 'Existing source cache is not a Git checkout; refusing to overwrite it.'
    }
} else {
    if ($VerifyOnly) { throw 'Missing source cache.' }
    # Vendored upstream samples include long Windows paths. Scope the setting
    # to this newly acquired checkout, never change the user's global Git config.
    & git clone --config core.longpaths=true --no-checkout --filter=blob:none $candidate.repository $source
    if ($LASTEXITCODE -ne 0) { throw 'ARMSX2 source acquisition failed.' }
    & git -C $source checkout --detach $candidate.revision
    if ($LASTEXITCODE -ne 0) { throw 'Pinned ARMSX2 checkout failed.' }
}
$origin = (& git -C $source remote get-url origin).Trim()
if ($LASTEXITCODE -ne 0 -or $origin -ne $candidate.repository) { throw 'Unexpected ARMSX2 origin.' }
$head = (& git -C $source rev-parse HEAD).Trim()
if ($LASTEXITCODE -ne 0 -or $head -ne $candidate.revision) { throw 'Unexpected ARMSX2 source revision.' }
$dirty = @(& git -C $source status --porcelain --untracked-files=all)
if ($LASTEXITCODE -ne 0 -or $dirty.Count) { throw 'Unaudited changes in ARMSX2 source cache.' }
$gitlinks = @(& git -C $source ls-files --stage | Where-Object { $_ -match '^160000 ' })
if ($LASTEXITCODE -ne 0 -or $gitlinks.Count) { throw 'Uninventoried submodule dependency in ARMSX2 source cache.' }
foreach ($relative in $candidate.required_paths) {
    if (-not (Test-Path -LiteralPath (Join-Path $source $relative) -PathType Leaf)) {
        throw "Incomplete candidate sources: $relative"
    }
}
$notice = Get-FileHash -Algorithm SHA256 -LiteralPath (Join-Path $source $candidate.license_file)
if ($notice.Hash -ne $candidate.license_sha256) { throw 'Changed ARMSX2 license notice.' }
if ($DependencyDir) {
    $deps = [IO.Path]::GetFullPath($DependencyDir).TrimEnd('\', '/')
    if ($deps -eq [IO.Path]::GetPathRoot($deps).TrimEnd('\', '/') -or
            $deps -eq $project -or $deps -eq $source -or
            $deps.StartsWith($project+'\', [StringComparison]::OrdinalIgnoreCase) -or
            $project.StartsWith($deps+'\', [StringComparison]::OrdinalIgnoreCase) -or
            $deps.StartsWith($source+'\', [StringComparison]::OrdinalIgnoreCase) -or
            $source.StartsWith($deps+'\', [StringComparison]::OrdinalIgnoreCase)) {
        throw 'Use a separate external dependency cache, not project/source or ancestor/drive root.'
    }
    if (Test-Path -LiteralPath $deps) {
        if ((Get-Item -LiteralPath $deps).Attributes -band [IO.FileAttributes]::ReparsePoint) {
            throw 'Refusing a reparse-point dependency cache.'
        }
    } elseif ($VerifyOnly) {
        throw 'Missing dependency cache.'
    } else {
        New-Item -ItemType Directory -Path $deps | Out-Null
    }
    $shaderDeps = Get-Content -LiteralPath (Join-Path $source 'platforms/android/app/src/main/cpp/3rdparty/shaderc/DEPS') -Raw
    foreach ($dependency in $candidate.external_dependencies) {
        if ($shaderDeps -notmatch [Regex]::Escape($dependency.revision)) {
            throw 'Candidate external dependency no longer matches vendored shaderc DEPS.'
        }
        $dep = Join-Path $deps $dependency.name
        if (Test-Path -LiteralPath $dep) {
            if ((Get-Item -LiteralPath $dep).Attributes -band [IO.FileAttributes]::ReparsePoint) {
                throw 'Refusing a reparse-point dependency checkout.'
            }
            if (-not (Test-Path -LiteralPath (Join-Path $dep '.git'))) {
                throw 'Existing dependency cache is not a checkout; refusing to overwrite it.'
            }
        } elseif ($VerifyOnly) {
            throw "Missing external dependency: $($dependency.name)"
        } else {
            & git -c core.longpaths=true init --quiet $dep
            if ($LASTEXITCODE -ne 0) { throw 'Dependency initialization failed.' }
            & git -C $dep remote add origin $dependency.repository
            if ($LASTEXITCODE -ne 0) { throw 'Dependency origin initialization failed.' }
            & git -C $dep fetch --depth 1 origin $dependency.revision
            if ($LASTEXITCODE -ne 0) { throw 'Pinned dependency acquisition failed.' }
            & git -C $dep checkout --detach FETCH_HEAD
            if ($LASTEXITCODE -ne 0) { throw 'Pinned dependency checkout failed.' }
        }
        $depOrigin = (& git -C $dep remote get-url origin).Trim()
        if ($LASTEXITCODE -ne 0 -or $depOrigin -ne $dependency.repository) { throw 'Unexpected dependency origin.' }
        $depRevision = (& git -C $dep rev-parse HEAD).Trim()
        if ($LASTEXITCODE -ne 0 -or $depRevision -ne $dependency.revision) { throw 'Unexpected dependency revision.' }
        $depDirty = @(& git -C $dep status --porcelain --untracked-files=all)
        if ($LASTEXITCODE -ne 0 -or $depDirty.Count) { throw 'Unaudited dependency source changes.' }
        $depGitlinks = @(& git -C $dep ls-files --stage | Where-Object { $_ -match '^160000 ' })
        if ($LASTEXITCODE -ne 0 -or $depGitlinks.Count) { throw 'Uninventoried nested dependency.' }
        if (-not (Test-Path -LiteralPath (Join-Path $dep 'CMakeLists.txt') -PathType Leaf)) {
            throw 'External dependency sources incomplete.'
        }
        Write-Output "External dependency verified: $($dependency.name) $depRevision"
    }
} else {
    Write-Output 'External shaderc dependencies still require separate acquisition/verification with -DependencyDir.'
}
Write-Output "ARMSX2 candidate source verified: $head. Not a product-build or distribution certificate."
