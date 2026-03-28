@echo off

setlocal
REM this builds libjxl and replaces the libs in the JPEGView src folder

SET XSRC_DIR=%~dp0..\..\src
SET XLIB_DIR=%~dp0..\third_party\libjxl
SET XOUT_DIR=%~dp0libjxl

IF EXIST "%XOUT_DIR%" (
	rmdir /s/q "%XOUT_DIR%"
	IF ERRORLEVEL 1 exit /b 1
)

call :BUILD_COPY_JXL x86 Win32 ""
IF ERRORLEVEL 1 exit /b 1

call :BUILD_COPY_JXL x64 x64 "64"
IF ERRORLEVEL 1 exit /b 1


echo === HEADER FILES ===
echo Copying 'extras\scripts\libjxl\x64\lib\include\jxl\*' -to- 'src\JPEGView\libjxl\include\jxl'
copy /y "%XOUT_DIR%\x64\lib\include\jxl\*" "%XSRC_DIR%\JPEGView\libjxl\include\jxl\"

exit /b 0





:BUILD_COPY_JXL

REM so the environments don't pollute each other
setlocal

SET XBUILD_ARCH=%~1
SET XPLATFORM=%~2
SET XJPV_ARCH_PATH=%~3

SET XBUILD_DIR=%XOUT_DIR%\%XBUILD_ARCH%

mkdir "%XBUILD_DIR%" 2>nul

call "%~dp0vs-init.bat" %XBUILD_ARCH%

pushd "%XBUILD_DIR%"

cmake.exe -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=OFF -A %XPLATFORM% "%XLIB_DIR%"
IF ERRORLEVEL 1 exit /b 1
msbuild.exe -p:Platform=%XPLATFORM% -p:configuration="Release" LIBJXL.sln -t:brotlicommon;brotlidec;brotlienc;jxl;jxl_cms;jxl_dec;jxl_threads
IF ERRORLEVEL 1 exit /b 1

popd

REM copy the libs over
copy /y "%XBUILD_DIR%\third_party\brotli\Release\brotlicommon.dll" "%XSRC_DIR%\JPEGView\libjxl\bin%XJPV_ARCH_PATH%\"
IF ERRORLEVEL 1 exit /b 1
copy /y "%XBUILD_DIR%\third_party\brotli\Release\brotlidec.dll" "%XSRC_DIR%\JPEGView\libjxl\bin%XJPV_ARCH_PATH%\"
IF ERRORLEVEL 1 exit /b 1

copy /y "%XBUILD_DIR%\lib\Release\jxl_dec.dll" "%XSRC_DIR%\JPEGView\libjxl\bin%XJPV_ARCH_PATH%\"
IF ERRORLEVEL 1 exit /b 1
copy /y "%XBUILD_DIR%\lib\Release\jxl_threads.dll" "%XSRC_DIR%\JPEGView\libjxl\bin%XJPV_ARCH_PATH%\"
IF ERRORLEVEL 1 exit /b 1

copy /y "%XBUILD_DIR%\lib\Release\jxl_dec.lib" "%XSRC_DIR%\JPEGView\libjxl\lib%XJPV_ARCH_PATH%\"
IF ERRORLEVEL 1 exit /b 1
copy /y "%XBUILD_DIR%\lib\Release\jxl_threads.lib" "%XSRC_DIR%\JPEGView\libjxl\lib%XJPV_ARCH_PATH%\"
IF ERRORLEVEL 1 exit /b 1





exit /b 0
