@echo off
rem Runs the given command inside an x64 MSVC developer environment,
rem preferring the CMake and Ninja that ship with Visual Studio.
rem Example: scripts\msvc.cmd cmake --build build
setlocal
set "PF86=%ProgramFiles(x86)%"
rem Git Bash drops env vars with parentheses in their name.
if "%PF86%"=="" set "PF86=%SystemDrive%\Program Files (x86)"
set "PATH=%PF86%\Microsoft Visual Studio\Installer;%PATH%"
for /f "tokens=*" %%i in ('vswhere.exe -latest -products * -version [17.0^,18.0^) -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath') do set "VSROOT=%%i"
if not defined VSROOT (echo Visual Studio with C++ tools not found & exit /b 1)
call "%VSROOT%\VC\Auxiliary\Build\vcvars64.bat" >nul || exit /b 1
set "PATH=%VSROOT%\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin;%VSROOT%\Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja;%PATH%"
%*
