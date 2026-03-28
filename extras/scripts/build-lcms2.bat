@echo off

setlocal
REM this builds lcms2 and replaces the dlls/libs in the JPEGView src folder

SET XSRC_DIR=%~dp0..\..\src\JPEGView\lcms2
SET XLIB_DIR=%~dp0..\third_party\Little-CMS

SET XVS_VER=2022
IF /I "%XVS_INIT_VER%" NEQ "" (
	REM override the build version for the solutions provided
	SET XVS_VER=%XVS_INIT_VER%
)


call :BUILD_COPY_LCMS x86 Win32 ""
IF ERRORLEVEL 1 exit /b 1

call :BUILD_COPY_LCMS x64 x64 "64"
IF ERRORLEVEL 1 exit /b 1


echo === HEADER FILES ===
echo Copying 'extras\third_party\Little-CMS\include\lcms2.h' -to- 'src\JPEGView\lcms2\include\'
copy /y "%XLIB_DIR%\include\lcms2.h" "%XSRC_DIR%\include\"

exit /b 0




REM ===============================================================================================

:BUILD_COPY_LCMS

REM so the environments don't pollute each other
setlocal

call "%~dp0vs-init.bat" %1

pushd "%XLIB_DIR%"

REM delete any previous build files. This is neccessary since x86 and x64 version are written to the same folder.
del "bin\lcms2.*" 2>nul

msbuild /t:lcms2_DLL /p:Platform=%2 /p:Configuration=Release .\Projects\VC%XVS_VER%\lcms2.sln
IF ERRORLEVEL 1 exit /b 1

copy /y "bin\lcms2.lib" "%XSRC_DIR%\lib%~3"
IF ERRORLEVEL 1 exit /b 1
copy /y "bin\lcms2.dll" "%XSRC_DIR%\bin%~3"
IF ERRORLEVEL 1 exit /b 1

exit /b 0
