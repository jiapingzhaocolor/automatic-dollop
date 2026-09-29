param(
    [string]$BundlePath = ".\FilmGrainOFX.ofx.bundle"
)

$ErrorActionPreference = "Stop"
$destination = Join-Path $env:ProgramFiles "Common Files\OFX\Plugins\FilmGrainOFX.ofx.bundle"

if (-not (Test-Path $BundlePath)) {
    throw "Bundle not found: $BundlePath"
}

Write-Host "Installing to $destination"
New-Item -ItemType Directory -Force -Path (Split-Path $destination) | Out-Null
if (Test-Path $destination) {
    Remove-Item -Recurse -Force $destination
}
Copy-Item -Recurse -Force $BundlePath $destination
Write-Host "Installed. Restart DaVinci Resolve."
