
rem TODO 64-bit version
set "VCPATH=C:\\Program Files (x86)\\Microsoft Visual Studio 14.0\\VC\\"

rem set up the environment of the console for VS2015
call "%VCPATH%vcvarsall.bat"

rem /LD specifies generating a dll
rem Windows doesn't have -undefined dynamic_lookup, so need to explicitly refer to the MaxAPI etc. libs
"%VCPATH%\\bin\\cl.exe" /LD /I "%cd%\\..\\source" dyn_test.cpp "%cd%\\..\\..\\max-sdk\\source\\c74support\\max-includes\\MaxAPI.lib"

rem couldn't find a way to stop cl.exe from generating these, so clean up after
del dyn_test.obj dyn_test.exp dyn_test.lib

echo ok