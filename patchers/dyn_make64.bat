
rem TODO 64-bit version
set "VCPATH=C:\\Program Files (x86)\\Microsoft Visual Studio 14.0\\VC\\"

rem set up the environment of the console for VS2015
call "%VCPATH%vcvarsall.bat" x86_amd64

rem create a dll referring to the Max libs
"%VCPATH%\\bin\\cl.exe" /D "WIN_VERSION" /D "WIN32" /D "NDEBUG" /O2 dyn_test.cpp /I "%cd%\\..\\source" /I "%cd%\\..\\..\\max-sdk\\source\\c74support\\max-includes\\" /I "%cd%\\..\\..\\max-sdk\\source\\c74support\\msp-includes\\" /I "%cd%\\..\\..\\max-sdk\\source\\c74support\\jit-includes\\" /link /DLL /MACHINE:X64 "%cd%\\..\\..\\max-sdk\\source\\c74support\\max-includes\\x64\\MaxAPI.lib" "%cd%\\..\\..\\max-sdk\\source\\c74support\\msp-includes\\x64\\MaxAudio.lib" "%cd%\\..\\..\\max-sdk\\source\\c74support\\jit-includes\\x64\\jitlib.lib" 

rem couldn't find a way to stop cl.exe from generating these, so clean up after
del dyn_test.obj dyn_test.exp dyn_test.lib

echo ok

rem x86 build
rem /GS- /GL /analyze- /W3 /Gy- /Zc:wchar_t /I".." /Zi /Gm- /O2 /Ob2 /Fd"sysbuild\intermediate\Release_Win32\dyn\dyn.pdb" /Zc:inline /fp:precise /D "WIN_VERSION" /D "WIN32" /D "NDEBUG" /D "_WINDOWS" /D "_USRDLL" /D "WIN_EXT_VERSION" /D "_CRT_SECURE_NO_WARNINGS" /D "_VC80_UPGRADE=0x0710" /D "_WINDLL" /D "_MBCS" /errorReport:prompt /GF /WX- /Zc:forScope /arch:SSE2 /Gd /Oy /Oi /MD /Fa"sysbuild\intermediate\Release_Win32\dyn\dyn.asm" /nologo /Fo"sysbuild\intermediate\Release_Win32\dyn\" /Ot /Fp"sysbuild\intermediate\Release_Win32\dyn\dyn.pch" 

rem /OUT:"..\..\externals\dyn.mxe" /MANIFEST /LTCG:incremental /PDB:"sysbuild\intermediate\Release_Win32\dyn\dyn.pdb" /DYNAMICBASE:NO "MaxAPI.lib" "MaxAudio.lib" "jitlib.lib" "kernel32.lib" "user32.lib" "gdi32.lib" "winspool.lib" "comdlg32.lib" "advapi32.lib" "shell32.lib" "ole32.lib" "oleaut32.lib" "uuid.lib" "odbc32.lib" "odbccp32.lib" /IMPLIB:"sysbuild\intermediate\Release_Win32\dyn\dyn.lib" /DLL /MACHINE:X86 /NODEFAULTLIB:"libcmt.lib" /OPT:REF /SAFESEH /INCREMENTAL:NO /PGD:"..\..\externals\dyn.pgd" /SUBSYSTEM:WINDOWS /MANIFESTUAC:"level='asInvoker' uiAccess='false'" /ManifestFile:"sysbuild\intermediate\Release_Win32\dyn\dyn.mxe.intermediate.manifest" /OPT:ICF /ERRORREPORT:PROMPT /NOLOGO /LIBPATH:"C:\Users\user\Documents\Max 7\Packages\Max_Worldmaking_Package\source\dyn\..\..\..\max-sdk\source\c74support\max-includes" /LIBPATH:"C:\Users\user\Documents\Max 7\Packages\Max_Worldmaking_Package\source\dyn\..\..\..\max-sdk\source\c74support\msp-includes" /LIBPATH:"C:\Users\user\Documents\Max 7\Packages\Max_Worldmaking_Package\source\dyn\..\..\..\max-sdk\source\c74support\jit-includes" /TLBID:1 

rem x64 build 
rem no /analyze-  /arch:SSE2
rem 

rem /OUT:"..\..\externals\dyn.mxe64" /MANIFEST /LTCG:incremental /PDB:"sysbuild\intermediate\Release_x64\dyn\dyn.pdb" /DYNAMICBASE:NO "MaxAPI.lib" "MaxAudio.lib" "jitlib.lib" "kernel32.lib" "user32.lib" "gdi32.lib" "winspool.lib" "comdlg32.lib" "advapi32.lib" "shell32.lib" "ole32.lib" "oleaut32.lib" "uuid.lib" "odbc32.lib" "odbccp32.lib" /IMPLIB:"sysbuild\intermediate\Release_x64\dyn\dyn.lib" /DLL /MACHINE:X64 /NODEFAULTLIB:"libcmt.lib" /OPT:REF /INCREMENTAL:NO /PGD:"..\..\externals\dyn.pgd" /SUBSYSTEM:WINDOWS /MANIFESTUAC:"level='asInvoker' uiAccess='false'" /ManifestFile:"sysbuild\intermediate\Release_x64\dyn\dyn.mxe64.intermediate.manifest" /OPT:ICF /ERRORREPORT:PROMPT /NOLOGO /LIBPATH:"C:\Users\user\Documents\Max 7\Packages\Max_Worldmaking_Package\source\dyn\..\..\..\max-sdk\source\c74support\max-includes\x64" /LIBPATH:"C:\Users\user\Documents\Max 7\Packages\Max_Worldmaking_Package\source\dyn\..\..\..\max-sdk\source\c74support\msp-includes\x64" /LIBPATH:"C:\Users\user\Documents\Max 7\Packages\Max_Worldmaking_Package\source\dyn\..\..\..\max-sdk\source\c74support\jit-includes\x64" /TLBID:1 