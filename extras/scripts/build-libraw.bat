@echo off

setlocal
REM this builds LibRaw and replaces the dlls/libs in the JPEGView src folder

SET XSRC_DIR=%~dp0..\..\src\JPEGView\libraw
SET XLIB_DIR=%~dp0..\third_party\LibRaw

SET XVS_VER=2022
IF /I "%XVS_INIT_VER%" NEQ "" (
	REM override the build version for the solutions provided
	SET XVS_VER=%XVS_INIT_VER%
)

REM random name of a makefile that we're going to create / patch with options
SET XMAKEFILE=Makefile.msvc-%RANDOM%
SET XMF_PATH=%XLIB_DIR%\%XMAKEFILE%

IF EXIST "%XMF_PATH%" (
	echo ERROR: Random Makefile exists?  Shouldn't happen... reset submodule please!
	exit /b 1
)

call :PATCH_LIBRAW_MAKEFILE
IF ERRORLEVEL 1 exit /b 1

call :BUILD_COPY_LIBRAW x86 ""
IF ERRORLEVEL 1 exit /b 1

call :BUILD_COPY_LIBRAW x64 "64"
IF ERRORLEVEL 1 exit /b 1

echo === HEADER FILES ===
echo Copying 'extras\third_party\LibRaw\libraw\*' -to- 'src\JPEGView\libraw\include\libraw\'
copy /y "%XLIB_DIR%\libraw\*" "%XSRC_DIR%\include\libraw\"

REM cleanup
call :CLEANUP

exit /b 0




REM ===============================================================================================

:PATCH_LIBRAW_MAKEFILE

REM patch the Makefile so that it has the options we need.  This only has to be done once

echo + Patching Makefile with extra options ...

setlocal

pushd "%XLIB_DIR%"

SET XTMP_FILE=%TEMP%\libraw-makefile-%RANDOM%-%RANDOM%.tmp

IF EXIST "%XTMP_FILE%" (
	echo ERROR: random temp file exists?!  impossible...
	exit /b 1
)

REM -- add these options at the top --

REM add in the extra features compile flags
>> "%XTMP_FILE%" echo COPT_OPT=/DUSE_X3FTOOLS /DUSE_6BY9RPI /DUSE_OLD_VIDEOCAMS

REM -- finally concat the file --

copy "%XTMP_FILE%" /A + Makefile.msvc /A "%XMAKEFILE%" /A
IF ERRORLEVEL 1 exit /b 1

REM cleanup
del "%XTMP_FILE%"

popd

exit /b 0


REM ===============================================================================================

:BUILD_COPY_LIBRAW

REM so the environments don't pollute each other
setlocal

call "%~dp0vs-init.bat" %1

pushd "%XLIB_DIR%"

REM delete any previous build files, if exists
nmake.exe -f "%XMAKEFILE%" clean
IF ERRORLEVEL 1 exit /b 1

nmake.exe -f "%XMAKEFILE%"
IF ERRORLEVEL 1 exit /b 1


copy /y "lib\libraw.lib" "%XSRC_DIR%\lib%~2"
IF ERRORLEVEL 1 exit /b 1
copy /y "bin\libraw.dll" "%XSRC_DIR%\bin%~2"
IF ERRORLEVEL 1 exit /b 1

exit /b 0


REM ===============================================================================================

:CLEANUP

del "%XMF_PATH%"

del /s /q "%XLIB_DIR%\bin\*.exe"
del /s /q "%XLIB_DIR%\bin\*.dll"
del /s /q "%XLIB_DIR%\lib\*.exp"
del /s /q "%XLIB_DIR%\lib\*.lib"
del /s /q "%XLIB_DIR%\object\*.obj"

exit /b 0
