# fetch.ps1 - every Changeling in apps.txt: cloned if it's missing, pulled if it's here.
Set-Location $PSScriptRoot
foreach ($line in Get-Content apps.txt) {
    $app = $line.Trim()
    if (-not $app -or $app.StartsWith('#')) { continue }
    if (Test-Path "$app\.git") {
        Write-Host "== ${app}: pulling"
        git -C $app -c http.version=HTTP/1.1 pull --ff-only
    } else {
        Write-Host "== ${app}: cloning"
        git -c http.version=HTTP/1.1 clone "https://github.com/CursedPrograms/$app.git" $app
    }
}
