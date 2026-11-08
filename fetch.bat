@echo off
rem fetch.bat - every Changeling in apps.txt: cloned if it's missing, pulled if it's here.
cd /d "%~dp0"
for /f "usebackq eol=# tokens=*" %%A in ("apps.txt") do (
    if exist "%%A\.git" (
        echo == %%A: pulling
        git -C "%%A" -c http.version=HTTP/1.1 pull --ff-only
    ) else (
        echo == %%A: cloning
        git -c http.version=HTTP/1.1 clone "https://github.com/CursedPrograms/%%A.git" "%%A"
    )
)
pause
