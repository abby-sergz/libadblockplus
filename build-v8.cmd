@echo off
SETLOCAL

rem %1 - arch {ia32, x64}
rem %2 - configuration {Release, Debug}
set ARCH=%~1
set CONFIGURATION=%~2

pushd "%~dp0"

@python msvs_gyp_wrapper.py --depth=build\%ARCH%\v8 -f msvs -I v8.gypi --generator-output=build\%ARCH%\v8 -G msvs_version=2015 -Dtarget_arch=%ARCH% -Dhost_arch=%ARCH% third_party/v8/src/v8.gyp

@call "c:\Program Files (x86)\Microsoft Visual Studio 14.0\vc\bin\vcvars32.bat"

@msbuild /m build/%ARCH%/v8/third_party/v8/src/v8.sln /p:Configuration=%CONFIGURATION% /target:v8_maybe_snapshot,v8_libplatform,v8_libsampler

popd

ENDLOCAL

exit /b 0
