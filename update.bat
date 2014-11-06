::%comspec% /k ""D:\Program Files\Microsoft Visual Studio 8\VC\vcvarsall.bat"" x86
::call "D:\Program Files\Microsoft Visual Studio 8\VC\vcvarsall.bat" x86
call "D:\Program Files (x86)\Microsoft Visual Studio 10.0\VC\vcvarsall.bat" x86

set PATH=%PATH%;E:\chromium\depot_tools;
cmd