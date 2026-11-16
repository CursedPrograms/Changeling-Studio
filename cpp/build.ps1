# Builds Changeling Studio (C++): the same steps as build.bat (SDL2 and ONNX Runtime fetched once, then CMake).
$ErrorActionPreference = 'Stop'
Set-Location $PSScriptRoot
& cmd /c "`"$PSScriptRoot\build.bat`""
exit $LASTEXITCODE
