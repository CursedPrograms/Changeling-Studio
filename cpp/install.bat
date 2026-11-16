@echo off
setlocal
cd /d "%~dp0"
rem Installs Changeling Studio (C++) for this user: %LOCALAPPDATA%\Programs\Changeling Studio (where the Atelier
rem launcher finds it), with Start menu and Desktop shortcuts. Run build.bat first (or unzip a release and run this beside it).
set "SRC=dist\ChangelingStudio"
if exist "ChangelingStudio.exe" set "SRC=."
if not exist "%SRC%\ChangelingStudio.exe" ( echo Build it first: build.bat & exit /b 1 )
tasklist /fi "imagename eq ChangelingStudio.exe" | find /i "ChangelingStudio.exe" >nul
if not errorlevel 1 ( echo Changeling Studio is open: close it and run this again. & exit /b 1 )
set "INST=%LOCALAPPDATA%\Programs\Changeling Studio"
if not exist "%INST%" mkdir "%INST%"
xcopy /e /i /q /y "%SRC%\*" "%INST%\" >nul || ( echo Couldn't copy to %INST% & exit /b 1 )
powershell -NoProfile -Command "$s=New-Object -ComObject WScript.Shell; foreach($d in @([Environment]::GetFolderPath('Programs'),[Environment]::GetFolderPath('Desktop'))){ $l=$s.CreateShortcut((Join-Path $d 'Changeling Studio.lnk')); $l.TargetPath='%INST%\ChangelingStudio.exe'; $l.WorkingDirectory='%INST%'; $l.IconLocation='%INST%\ChangelingStudio.exe,0'; $l.Description='Changeling Studio: every Changeling in one window'; $l.Save() }"
echo Installed: %INST%
exit /b 0
