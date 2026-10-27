# Runs this project's own PlatformIO, kept apart from any other PlatformIO install (~\.platformio).
# Usage: .\pio.ps1 run                              build
#        .\pio.ps1 run -t upload                    build and flash (port from platformio.ini)
#        .\pio.ps1 run -t upload --upload-port COM5 build and flash to another port
# The first call creates .piocore\venv with PlatformIO Core 6.2.0; toolchains then download into .piocore\core.
# Not 'Stop': PlatformIO writes progress to stderr, which PowerShell 5.1 would turn into a terminating error.
$ErrorActionPreference = 'Continue'
$root = $PSScriptRoot
$venv = Join-Path $root '.piocore\venv'
$pio  = Join-Path $venv 'Scripts\pio.exe'

if (-not (Test-Path $pio)) {
  # Python 3.11 from python.org or winget (the py launcher); the WindowsApps "python" is only a Store stub
  $py = $null
  if (Get-Command py -ErrorAction SilentlyContinue) {
    & py -3.11 -c "pass" 2>$null
    if ($LASTEXITCODE -eq 0) { $py = @('py', '-3.11') }
  }
  if (-not $py) {
    Write-Host 'Python 3.11 not found. Install it with: winget install --id Python.Python.3.11 -e'
    exit 1
  }
  Write-Host "Creating $venv"
  & $py[0] $py[1] -m venv $venv
  if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
  & (Join-Path $venv 'Scripts\python.exe') -m pip install --disable-pip-version-check 'platformio==6.2.0'
  if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
}
$env:PLATFORMIO_CORE_DIR = Join-Path $root '.piocore\core'
$env:ELECTRON_RUN_AS_NODE = $null
Set-Location $root
& $pio @args
exit $LASTEXITCODE
