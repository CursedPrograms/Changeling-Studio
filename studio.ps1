# Changeling Studio: 1. get every Changeling (git), 2. their requirements, 3. their models (each downloaded once and
# shared), then the Studio window. .\studio.ps1 window opens only the window.
Set-Location $PSScriptRoot
if (Get-Command py -ErrorAction SilentlyContinue) { py -3 studio.py @args }
elseif (Get-Command python -ErrorAction SilentlyContinue) { python studio.py @args }
else { Write-Host "Changeling Studio needs Python 3.9 or newer: https://www.python.org/downloads/"; exit 1 }
if ($LASTEXITCODE) { Read-Host "Press Enter" }
