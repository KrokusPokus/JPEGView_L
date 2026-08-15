@echo off

setlocal

SET EXPORT_DIR=%~dp0..\..\src\JPEGView\libpng
SET XPATCH_DIR=%~dp0..\third_party\libpng

SET ZLibSrcDir=%XPATCH_DIR%\zlib
SET ZLIB_BUILD_DIR=%~dp0build_zlib

SET LIBPNG_Source_Dir=%XPATCH_DIR%\libpng
SET LIBPNG_BUILD_DIR=%~dp0build_libpng


REM cleanup
call :CLEANUP

REM build libraries
call :BUILD_PNG x86 Win32
IF ERRORLEVEL 1 exit /b 1
call :BUILD_PNG x64 x64
IF ERRORLEVEL 1 exit /b 1


REM copy the libs over
mkdir "%EXPORT_DIR%\include"
mkdir "%EXPORT_DIR%\lib"
mkdir "%EXPORT_DIR%\lib64"

copy /y "%LIBPNG_BUILD_DIR%\x86\libpng18_static.lib" "%EXPORT_DIR%\lib\"
IF ERRORLEVEL 1 (
	echo Failed copy "%LIBPNG_BUILD_DIR%\x86\libpng18_static.lib" "%EXPORT_DIR%\lib\"
	exit /b 1
)

copy /y "%ZLIB_BUILD_DIR%\x86\libzs.lib" "%EXPORT_DIR%\lib\zlib.lib"
IF ERRORLEVEL 1 (
	echo Failed copy "%ZLIB_BUILD_DIR%\x86\libzs.lib" "%EXPORT_DIR%\lib\zlib.lib"
	exit /b 1
)

copy /y "%LIBPNG_BUILD_DIR%\x64\libpng18_static.lib" "%EXPORT_DIR%\lib64\"
IF ERRORLEVEL 1 (
	echo Failed copy "%LIBPNG_BUILD_DIR%\x64\libpng18_static.lib" "%EXPORT_DIR%\lib64\"
	exit /b 1
)

copy /y "%ZLIB_BUILD_DIR%\x64\libzs.lib" "%EXPORT_DIR%\lib64\zlib.lib"
IF ERRORLEVEL 1 (
	echo Failed copy "%ZLIB_BUILD_DIR%\x64\libzs.lib" "%EXPORT_DIR%\lib64\zlib.lib"
	exit /b 1
)

copy /y "%LIBPNG_Source_Dir%\png.h" "%EXPORT_DIR%\include\"
IF ERRORLEVEL 1 (
	echo Failed copy "%LIBPNG_Source_Dir%\png.h" "%EXPORT_DIR%\include\"
	exit /b 1
)

copy /y "%LIBPNG_Source_Dir%\pngconf.h" "%EXPORT_DIR%\include\"
IF ERRORLEVEL 1 (
	echo Failed copy "%LIBPNG_Source_Dir%\pngconf.h" "%EXPORT_DIR%\include\"
	exit /b 1
)

copy /y "%LIBPNG_Source_Dir%\pnglibconf.h.prebuilt" "%EXPORT_DIR%\include\pnglibconf.h"
IF ERRORLEVEL 1 (
	echo Failed copy "%LIBPNG_Source_Dir%\pnglibconf.h.prebuilt" "%EXPORT_DIR%\include\pnglibconf.h"
	exit /b 1
)

exit /b 0




:BUILD_PNG

REM so the environments don't pollute each other
setlocal

call "%~dp0vs-init.bat" %1


mkdir "%ZLIB_BUILD_DIR%\%1%" >nul
pushd "%ZLIB_BUILD_DIR%\%1%"

cmake.exe -G"NMake Makefiles" -DCMAKE_MSVC_RUNTIME_LIBRARY="MultiThreaded" -DCMAKE_BUILD_TYPE=Release "%ZLibSrcDir%"
IF ERRORLEVEL 1 exit /b 1
nmake.exe
IF ERRORLEVEL 1 exit /b 1

REM ren z.dll zlib1.dll
del z.lib
ren zs.lib zlib.lib

copy /y "%ZLibSrcDir%\*.h" "%ZLIB_BUILD_DIR%\%1%\"
copy /y "%ZLibSrcDir%\*.c" "%ZLIB_BUILD_DIR%\%1%\"

popd


mkdir "%LIBPNG_BUILD_DIR%\%1%" >nul
pushd "%LIBPNG_BUILD_DIR%\%1%"

SET ZLIB_ROOT=%ZLIB_BUILD_DIR%\%1%

cmake.exe -G"NMake Makefiles" -DCMAKE_MSVC_RUNTIME_LIBRARY="MultiThreaded" -DCMAKE_BUILD_TYPE=Release "%LIBPNG_Source_Dir%"
IF ERRORLEVEL 1 exit /b 1
nmake.exe
IF ERRORLEVEL 1 exit /b 1

popd

endlocal
exit /b 0



:CLEANUP

del /s /q "%LIBPNG_Source_Dir%\*.rej"
del /s /q "%LIBPNG_Source_Dir%\*.orig"
del /s /q "%LIBPNG_Source_Dir%\pnglibconf.h"

cd "%LIBPNG_Source_Dir%
git restore .
cd "%~dp0"

IF EXIST "%ZLIB_BUILD_DIR%" (
	rmdir /s/q "%ZLIB_BUILD_DIR%"
	IF ERRORLEVEL 1 exit /b 1
)

IF EXIST "%LIBPNG_BUILD_DIR%" (
	rmdir /s/q "%LIBPNG_BUILD_DIR%"
	IF ERRORLEVEL 1 exit /b 1
)

REM IF EXIST "%EXPORT_DIR%" (
REM		rmdir /s/q "%EXPORT_DIR%"
REM 	IF ERRORLEVEL 1 exit /b 1
REM )

exit /b 0
