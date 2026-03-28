@echo off

setlocal
REM this builds lcms2 and replaces the dlls/libs in the JPEGView src folder

SET XSRC_DIR=%~dp0..\..\src\JPEGView\bit7z
SET XLIB_DIR=%~dp0..\third_party\bit7z
SET XOUT_DIR=%~dp0bit7z

IF EXIST "%XOUT_DIR%" (
	rmdir /s/q "%XOUT_DIR%"
	IF ERRORLEVEL 1 exit /b 1
)

IF EXIST "%XLIB_DIR%\lib" (
	rmdir /s/q "%XLIB_DIR%\lib"
	IF ERRORLEVEL 1 exit /b 1
)


call :BUILD_COPY_BIT7Z x86 Win32 ""
IF ERRORLEVEL 1 exit /b 1

call :BUILD_COPY_BIT7Z x64 x64 "64"
IF ERRORLEVEL 1 exit /b 1


echo === HEADER FILES ===
echo Copying 'extras\third_party\bit7z\include\bit7z\*' -to- 'src\JPEGView\bit7z\include\'
copy /y "%XLIB_DIR%\include\bit7z\*" "%XSRC_DIR%\include\"

exit /b 0




REM ===============================================================================================

:BUILD_COPY_BIT7Z

REM so the environments don't pollute each other
setlocal

SET XBUILD_ARCH=%~1
SET XPLATFORM=%~2
SET XJPV_ARCH_PATH=%~3

SET XBUILD_DIR=%XOUT_DIR%\%XBUILD_ARCH%

mkdir "%XBUILD_DIR%" 2>nul

call "%~dp0vs-init.bat" %XBUILD_ARCH%

pushd "%XBUILD_DIR%"

cmake -DCMAKE_BUILD_TYPE=Release -DBIT7Z_AUTO_FORMAT=ON -DBIT7Z_7ZIP_VERSION="26.00" -DBIT7Z_STATIC_RUNTIME=ON -A %XPLATFORM% "%XLIB_DIR%"

msbuild /t:bit7z -p:Platform=%XPLATFORM% /p:Configuration=Release bit7z.sln
IF ERRORLEVEL 1 exit /b 1

copy /y "%XLIB_DIR%\lib\%XBUILD_ARCH%\Release\bit7z.lib" "%XSRC_DIR%\lib%XJPV_ARCH_PATH%"
IF ERRORLEVEL 1 exit /b 1

exit /b 0
