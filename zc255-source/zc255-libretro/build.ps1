param([switch]$ConfigureOnly, [int]$Jobs = 4)
$ErrorActionPreference = 'Stop'
# The Python builder discovers installed Visual Studio/SDK tools without any
# hard-coded user paths, and sets its own subprocess build environment.
$buildArgs = @((Join-Path $PSScriptRoot 'build.py'), '--jobs', $Jobs)
if ($ConfigureOnly) { $buildArgs += '--configure-only' }
python @buildArgs
if ($LASTEXITCODE -ne 0) { throw 'Core build failed.' }
