@echo off
REM Build + run the ShackBook unit tests (opt-in target set).
REM Separate build dir from build.bat so a tests configure never disturbs the
REM app build the operator is running.

call "C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\VC\Auxiliary\Build\vcvars64.bat" >nul 2>&1
set PATH=%PATH%;C:\Program Files\CMake\bin;C:\Users\nigel\AppData\Local\Microsoft\WinGet\Packages\Ninja-build.Ninja_Microsoft.Winget.Source_8wekyb3d8bbwe;C:\Program Files\LLVM\bin

if not exist build-tests\CMakeCache.txt (
    echo Configuring tests with CMake...
    cmake -B build-tests -G Ninja -DCMAKE_BUILD_TYPE=Release -DSHACKBOOK_TESTS=ON -DCMAKE_PREFIX_PATH="C:/Qt/6.10.3/msvc2022_64"
    if errorlevel 1 goto :end
)

cmake --build build-tests -j %NUMBER_OF_PROCESSORS% %*

:end
echo.
echo Build exit code: %errorlevel%
