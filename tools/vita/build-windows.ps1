# Native Windows wrapper. Full intro is included unless --no-intro is supplied.
# Examples: ./tools/vita/build-windows.ps1
#           ./tools/vita/build-windows.ps1 --no-intro --jobs 4
$ErrorActionPreference = 'Stop'
$python = if (Test-Path -LiteralPath (Join-Path $PSScriptRoot '../../.venv/Scripts/python.exe')) {
    (Resolve-Path (Join-Path $PSScriptRoot '../../.venv/Scripts/python.exe')).Path
} elseif (Test-Path -LiteralPath (Join-Path $PSScriptRoot '../../sdk/host-python/Scripts/python.exe')) {
    (Resolve-Path (Join-Path $PSScriptRoot '../../sdk/host-python/Scripts/python.exe')).Path
} else { 'python' }
& $python (Join-Path $PSScriptRoot 'build.py') @args
if ($LASTEXITCODE) { exit $LASTEXITCODE }
