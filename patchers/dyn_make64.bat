
rem 64-bit version
@setlocal

rem first argument is the filename (minus the .cpp) to compile
rem e.g. dyn_make64.bat foo will try to compile foo.cpp into foo.x64.dll
IF [%1]==[] ( SET "INNAME=dyn_test" ) ELSE ( SET "INNAME=%1" )
SET OUTNAME=%INNAME%.x64

rem assume libraries etc. are found relative to current directory:
SET VSCMD_START_DIR=%cd%

rem set up environment for 64-bit compiling:
SET VCVARS64=VC\Auxiliary\Build\vcvars64.bat
if exist "%ProgramFiles(x86)%\Microsoft Visual Studio\2017\Enterprise\%VCVARS64%" call "%ProgramFiles(x86)%\Microsoft Visual Studio\2017\Enterprise\%VCVARS64%"
if exist "%ProgramFiles(x86)%\Microsoft Visual Studio\2017\Professional\%VCVARS64%" call "%ProgramFiles(x86)%\Microsoft Visual Studio\2017\Professional\%VCVARS64%"
if exist "%ProgramFiles(x86)%\Microsoft Visual Studio\2017\Community\%VCVARS64%" call "%ProgramFiles(x86)%\Microsoft Visual Studio\2017\Community\%VCVARS64%"
if exist "%ProgramFiles(x86)%\Microsoft Visual Studio\2017\BuildTools\%VCVARS64%" call "%ProgramFiles(x86)%\Microsoft Visual Studio\2017\BuildTools\%VCVARS64%"

rem compile it
cl /D "WIN_VERSION" /D "WIN64" /D "NDEBUG" /O2 "%INNAME%.cpp" /Fe: "%OUTNAME%.dll" /I "%cd%\\..\\source" /I "%cd%\\..\\..\\max-sdk\\source\\c74support\\max-includes\\" /I "%cd%\\..\\..\\max-sdk\\source\\c74support\\msp-includes\\" /I "%cd%\\..\\..\\max-sdk\\source\\c74support\\jit-includes\\" /link /DLL /MACHINE:X64 "%cd%\\..\\..\\max-sdk\\source\\c74support\\max-includes\\x64\\MaxAPI.lib" "%cd%\\..\\..\\max-sdk\\source\\c74support\\msp-includes\\x64\\MaxAudio.lib" "%cd%\\..\\..\\max-sdk\\source\\c74support\\jit-includes\\x64\\jitlib.lib" 

rem couldn't find a way to stop cl.exe from generating these, so clean up after
del "%OUTNAME%.obj" "%OUTNAME%.exp" "%OUTNAME%.lib"

echo ok