# Makes the F/A-18C draw its RWR into the "F18_RWR" viewport of the monitor setup, where
# receiver/rwr_stream.bat captures it for the board.
#
# DCS updates and repairs restore the original file, so run this again after one:
#   powershell -ExecutionPolicy Bypass -File Install-RwrViewport.ps1
# The original is kept beside it as RWR_ALR67_bake_init.lua.orig.
param([string]$DcsDir = "G:\DCS World")

$file = Join-Path $DcsDir "Mods\aircraft\FA-18C\Cockpit\Scripts\TEWS\indicator\BAKE\RWR_ALR67_bake_init.lua"
if (-not (Test-Path $file)) { Write-Error "Not found: $file (pass -DcsDir)"; exit 1 }

$marker = 'try_find_assigned_viewport("F18_RWR")'
if ((Get-Content $file -Raw).Contains($marker)) { Write-Host "Already installed: $file"; exit 0 }

if (-not (Test-Path "$file.orig")) { Copy-Item $file "$file.orig" }
# Same mechanism as the AMPCD's export (AMPCD_viewport_cfg.lua)
$patch = @"

-- F18_RWR: also draw the RWR into the F18_RWR viewport of the monitor setup, if it has one
dofile(LockOn_Options.common_script_path.."ViewportHandling.lua")
$marker
"@
Add-Content -Path $file -Value $patch -Encoding ascii
Write-Host "Installed: $file"
