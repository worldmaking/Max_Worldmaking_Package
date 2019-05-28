
rem 32-bit version
set "VCPATH=C:\\Program Files (x86)\\Microsoft Visual Studio 14.0\\VC\\"

rem set up the environment of the console for VS2015
call "%VCPATH%vcvarsall.bat"

rem create a dll referring to the Max libs
"%VCPATH%\\bin\\cl.exe" /LD /D "WIN_VERSION" /D "WIN32" /I "%cd%\\..\\source" /I "%cd%\\..\\..\\max-sdk\\source\\c74support\\max-includes\\" /I "%cd%\\..\\..\\max-sdk\\source\\c74support\\msp-includes\\" /I "%cd%\\..\\..\\max-sdk\\source\\c74support\\jit-includes\\" "%cd%\\..\\..\\max-sdk\\source\\c74support\\max-includes\\MaxAPI.lib" "%cd%\\..\\..\\max-sdk\\source\\c74support\\msp-includes\\MaxAudio.lib" "%cd%\\..\\..\\max-sdk\\source\\c74support\\jit-includes\\jitlib.lib" dyn_test.cpp 

rem couldn't find a way to stop cl.exe from generating these, so clean up after
del dyn_test.obj dyn_test.exp dyn_test.lib

echo ok