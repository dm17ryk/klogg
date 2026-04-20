echo %CILOGG_QT%
echo %CILOGG_QT_DIR%

md %CILOGG_WORKSPACE%\release

echo "Copying cilogg binaries..."
xcopy %CILOGG_WORKSPACE%\%CILOGG_BUILD_ROOT%\output\cilogg_portable.exe %CILOGG_WORKSPACE%\release\ /y
xcopy %CILOGG_WORKSPACE%\%CILOGG_BUILD_ROOT%\output\cilogg_portable.pdb %CILOGG_WORKSPACE%\release\ /y
xcopy %CILOGG_WORKSPACE%\%CILOGG_BUILD_ROOT%\output\cilogg.exe %CILOGG_WORKSPACE%\release\ /y
xcopy %CILOGG_WORKSPACE%\%CILOGG_BUILD_ROOT%\output\cilogg.pdb %CILOGG_WORKSPACE%\release\ /y

xcopy %CILOGG_WORKSPACE%\%CILOGG_BUILD_ROOT%\output\cilogg_crashpad_handler.exe %CILOGG_WORKSPACE%\release\ /y
xcopy %CILOGG_WORKSPACE%\%CILOGG_BUILD_ROOT%\output\cilogg_minidump_dump.exe %CILOGG_WORKSPACE%\release\ /y

xcopy %CILOGG_WORKSPACE%\%CILOGG_BUILD_ROOT%\msvc_19.41_cxx17_64_md_relwithdebinfo\tbb12.dll %CILOGG_WORKSPACE%\release\ /y
xcopy %CILOGG_WORKSPACE%\%CILOGG_BUILD_ROOT%\msvc_19.41_cxx17_64_md_relwithdebinfo\tbb12.pdb %CILOGG_WORKSPACE%\release\ /y
xcopy %CILOGG_WORKSPACE%\%CILOGG_BUILD_ROOT%\msvc_19.41_cxx17_32_md_relwithdebinfo\tbb12.dll %CILOGG_WORKSPACE%\release\ /y
xcopy %CILOGG_WORKSPACE%\%CILOGG_BUILD_ROOT%\msvc_19.41_cxx17_32_md_relwithdebinfo\tbb12.pdb %CILOGG_WORKSPACE%\release\ /y

xcopy %CILOGG_WORKSPACE%\%CILOGG_BUILD_ROOT%\msvc_19.42_cxx17_64_md_relwithdebinfo\tbb12.dll %CILOGG_WORKSPACE%\release\ /y
xcopy %CILOGG_WORKSPACE%\%CILOGG_BUILD_ROOT%\msvc_19.42_cxx17_64_md_relwithdebinfo\tbb12.pdb %CILOGG_WORKSPACE%\release\ /y
xcopy %CILOGG_WORKSPACE%\%CILOGG_BUILD_ROOT%\msvc_19.42_cxx17_32_md_relwithdebinfo\tbb12.dll %CILOGG_WORKSPACE%\release\ /y
xcopy %CILOGG_WORKSPACE%\%CILOGG_BUILD_ROOT%\msvc_19.42_cxx17_32_md_relwithdebinfo\tbb12.pdb %CILOGG_WORKSPACE%\release\ /y

xcopy %CILOGG_WORKSPACE%\%CILOGG_BUILD_ROOT%\msvc_19.42_cxx17_64_md_relwithdebinfo\tbbmalloc.dll %CILOGG_WORKSPACE%\release\ /y
xcopy %CILOGG_WORKSPACE%\%CILOGG_BUILD_ROOT%\msvc_19.42_cxx17_64_md_relwithdebinfo\tbbmalloc.pdb %CILOGG_WORKSPACE%\release\ /y
xcopy %CILOGG_WORKSPACE%\%CILOGG_BUILD_ROOT%\msvc_19.42_cxx17_32_md_relwithdebinfo\tbbmalloc.dll %CILOGG_WORKSPACE%\release\ /y
xcopy %CILOGG_WORKSPACE%\%CILOGG_BUILD_ROOT%\msvc_19.42_cxx17_32_md_relwithdebinfo\tbbmalloc.pdb %CILOGG_WORKSPACE%\release\ /y

xcopy %CILOGG_WORKSPACE%\%CILOGG_BUILD_ROOT%\msvc_19.42_cxx17_64_md_relwithdebinfo\tbbmalloc_proxy.dll %CILOGG_WORKSPACE%\release\ /y
xcopy %CILOGG_WORKSPACE%\%CILOGG_BUILD_ROOT%\msvc_19.42_cxx17_64_md_relwithdebinfo\tbbmalloc_proxy.pdb %CILOGG_WORKSPACE%\release\ /y
xcopy %CILOGG_WORKSPACE%\%CILOGG_BUILD_ROOT%\msvc_19.42_cxx17_32_md_relwithdebinfo\tbbmalloc_proxy.dll %CILOGG_WORKSPACE%\release\ /y
xcopy %CILOGG_WORKSPACE%\%CILOGG_BUILD_ROOT%\msvc_19.42_cxx17_32_md_relwithdebinfo\tbbmalloc_proxy.pdb %CILOGG_WORKSPACE%\release\ /y

xcopy %CILOGG_WORKSPACE%\%CILOGG_BUILD_ROOT%\msvc_19.44_cxx17_64_md_relwithdebinfo\tbb12.dll %CILOGG_WORKSPACE%\release\ /y
xcopy %CILOGG_WORKSPACE%\%CILOGG_BUILD_ROOT%\msvc_19.44_cxx17_64_md_relwithdebinfo\tbb12.pdb %CILOGG_WORKSPACE%\release\ /y
xcopy %CILOGG_WORKSPACE%\%CILOGG_BUILD_ROOT%\msvc_19.44_cxx17_32_md_relwithdebinfo\tbb12.dll %CILOGG_WORKSPACE%\release\ /y
xcopy %CILOGG_WORKSPACE%\%CILOGG_BUILD_ROOT%\msvc_19.44_cxx17_32_md_relwithdebinfo\tbb12.pdb %CILOGG_WORKSPACE%\release\ /y

xcopy %CILOGG_WORKSPACE%\%CILOGG_BUILD_ROOT%\msvc_19.44_cxx17_64_md_relwithdebinfo\tbbmalloc.dll %CILOGG_WORKSPACE%\release\ /y
xcopy %CILOGG_WORKSPACE%\%CILOGG_BUILD_ROOT%\msvc_19.44_cxx17_64_md_relwithdebinfo\tbbmalloc.pdb %CILOGG_WORKSPACE%\release\ /y
xcopy %CILOGG_WORKSPACE%\%CILOGG_BUILD_ROOT%\msvc_19.44_cxx17_32_md_relwithdebinfo\tbbmalloc.dll %CILOGG_WORKSPACE%\release\ /y
xcopy %CILOGG_WORKSPACE%\%CILOGG_BUILD_ROOT%\msvc_19.44_cxx17_32_md_relwithdebinfo\tbbmalloc.pdb %CILOGG_WORKSPACE%\release\ /y

xcopy %CILOGG_WORKSPACE%\%CILOGG_BUILD_ROOT%\msvc_19.44_cxx17_64_md_relwithdebinfo\tbbmalloc_proxy.dll %CILOGG_WORKSPACE%\release\ /y
xcopy %CILOGG_WORKSPACE%\%CILOGG_BUILD_ROOT%\msvc_19.44_cxx17_64_md_relwithdebinfo\tbbmalloc_proxy.pdb %CILOGG_WORKSPACE%\release\ /y
xcopy %CILOGG_WORKSPACE%\%CILOGG_BUILD_ROOT%\msvc_19.44_cxx17_32_md_relwithdebinfo\tbbmalloc_proxy.dll %CILOGG_WORKSPACE%\release\ /y
xcopy %CILOGG_WORKSPACE%\%CILOGG_BUILD_ROOT%\msvc_19.44_cxx17_32_md_relwithdebinfo\tbbmalloc_proxy.pdb %CILOGG_WORKSPACE%\release\ /y

xcopy %CILOGG_WORKSPACE%\%CILOGG_BUILD_ROOT%\msvc_19.44_cxx17_64_md_release\tbb12.dll %CILOGG_WORKSPACE%\release\ /y
xcopy %CILOGG_WORKSPACE%\%CILOGG_BUILD_ROOT%\msvc_19.44_cxx17_64_md_release\tbb12.pdb %CILOGG_WORKSPACE%\release\ /y
xcopy %CILOGG_WORKSPACE%\%CILOGG_BUILD_ROOT%\msvc_19.44_cxx17_32_md_release\tbb12.dll %CILOGG_WORKSPACE%\release\ /y
xcopy %CILOGG_WORKSPACE%\%CILOGG_BUILD_ROOT%\msvc_19.44_cxx17_32_md_release\tbb12.pdb %CILOGG_WORKSPACE%\release\ /y

xcopy %CILOGG_WORKSPACE%\%CILOGG_BUILD_ROOT%\msvc_19.44_cxx17_64_md_release\tbbmalloc.dll %CILOGG_WORKSPACE%\release\ /y
xcopy %CILOGG_WORKSPACE%\%CILOGG_BUILD_ROOT%\msvc_19.44_cxx17_64_md_release\tbbmalloc.pdb %CILOGG_WORKSPACE%\release\ /y
xcopy %CILOGG_WORKSPACE%\%CILOGG_BUILD_ROOT%\msvc_19.44_cxx17_32_md_release\tbbmalloc.dll %CILOGG_WORKSPACE%\release\ /y
xcopy %CILOGG_WORKSPACE%\%CILOGG_BUILD_ROOT%\msvc_19.44_cxx17_32_md_release\tbbmalloc.pdb %CILOGG_WORKSPACE%\release\ /y

xcopy %CILOGG_WORKSPACE%\%CILOGG_BUILD_ROOT%\msvc_19.44_cxx17_64_md_release\tbbmalloc_proxy.dll %CILOGG_WORKSPACE%\release\ /y
xcopy %CILOGG_WORKSPACE%\%CILOGG_BUILD_ROOT%\msvc_19.44_cxx17_64_md_release\tbbmalloc_proxy.pdb %CILOGG_WORKSPACE%\release\ /y
xcopy %CILOGG_WORKSPACE%\%CILOGG_BUILD_ROOT%\msvc_19.44_cxx17_32_md_release\tbbmalloc_proxy.dll %CILOGG_WORKSPACE%\release\ /y
xcopy %CILOGG_WORKSPACE%\%CILOGG_BUILD_ROOT%\msvc_19.44_cxx17_32_md_release\tbbmalloc_proxy.pdb %CILOGG_WORKSPACE%\release\ /y

xcopy %CILOGG_WORKSPACE%\%CILOGG_BUILD_ROOT%\generated\documentation.html %CILOGG_WORKSPACE%\release\ /y
xcopy %CILOGG_WORKSPACE%\COPYING %CILOGG_WORKSPACE%\release\ /y
xcopy %CILOGG_WORKSPACE%\NOTICE %CILOGG_WORKSPACE%\release\ /y
xcopy %CILOGG_WORKSPACE%\README.md %CILOGG_WORKSPACE%\release\ /y
xcopy %CILOGG_WORKSPACE%\DOCUMENTATION.md %CILOGG_WORKSPACE%\release\ /y
xcopy %CILOGG_WORKSPACE%\%CILOGG_BUILD_ROOT%\output\python_runtime %CILOGG_WORKSPACE%\release\python_runtime\ /e /i /y

echo "Copying vc runtime..."
xcopy "%VCToolsRedistDir%%platform%\Microsoft.VC143.CRT\msvcp140.dll" %CILOGG_WORKSPACE%\release\ /y
xcopy "%VCToolsRedistDir%%platform%\Microsoft.VC143.CRT\msvcp140_1.dll" %CILOGG_WORKSPACE%\release\ /y
xcopy "%VCToolsRedistDir%%platform%\Microsoft.VC143.CRT\msvcp140_2.dll" %CILOGG_WORKSPACE%\release\ /y
xcopy "%VCToolsRedistDir%%platform%\Microsoft.VC143.CRT\vcruntime140.dll" %CILOGG_WORKSPACE%\release\ /y
xcopy "%VCToolsRedistDir%%platform%\Microsoft.VC143.CRT\vcruntime140_1.dll" %CILOGG_WORKSPACE%\release\ /y

echo "Copying ssl..."
xcopy %SSL_DIR%\libcrypto-1_1%SSL_ARCH%.dll %CILOGG_WORKSPACE%\release\ /y
xcopy %SSL_DIR%\libssl-1_1%SSL_ARCH%.dll %CILOGG_WORKSPACE%\release\ /y

echo "Copying Qt..."
if "%CILOGG_QT_DIR%"=="" (
  echo ERROR: CILOGG_QT_DIR is empty
  exit /b 1
)
set "QTDIR=%CILOGG_QT_DIR:/=\%"
echo %QTDIR%
xcopy %QTDIR%\bin\%CILOGG_QT%Core.dll %CILOGG_WORKSPACE%\release\ /y
xcopy %QTDIR%\bin\%CILOGG_QT%Gui.dll %CILOGG_WORKSPACE%\release\ /y
xcopy %QTDIR%\bin\%CILOGG_QT%Network.dll %CILOGG_WORKSPACE%\release\ /y
xcopy %QTDIR%\bin\%CILOGG_QT%Widgets.dll %CILOGG_WORKSPACE%\release\ /y
xcopy %QTDIR%\bin\%CILOGG_QT%Concurrent.dll %CILOGG_WORKSPACE%\release\ /y
xcopy %QTDIR%\bin\%CILOGG_QT%Xml.dll %CILOGG_WORKSPACE%\release\ /y
xcopy %QTDIR%\bin\%CILOGG_QT%Core5Compat.dll %CILOGG_WORKSPACE%\release\ /y
xcopy %QTDIR%\bin\%CILOGG_QT%SerialPort.dll %CILOGG_WORKSPACE%\release\ /y
xcopy %QTDIR%\bin\%CILOGG_QT%Svg.dll %CILOGG_WORKSPACE%\release\ /y

echo "Deploying Qt runtime..."
if /i "%CILOGG_ARCH%"=="arm64" (
  call :manual_qt_deploy
  if errorlevel 1 exit /b 1
) else (
  "%QTDIR%\bin\windeployqt.exe" --force --no-compiler-runtime --dir %CILOGG_WORKSPACE%\release %CILOGG_WORKSPACE%\release\cilogg.exe
  if errorlevel 1 exit /b 1
)

echo "Copying packaging files..."
md %CILOGG_WORKSPACE%\chocolatey
xcopy %CILOGG_WORKSPACE%\packaging\windows\chocolatey\cilogg.nuspec chocolatey /y

md %CILOGG_WORKSPACE%\chocolatey\tools
xcopy %CILOGG_WORKSPACE%\packaging\windows\chocolatey\tools\chocolateyInstall.ps1 chocolatey\tools\ /y

xcopy %CILOGG_WORKSPACE%\packaging\windows\cilogg.nsi  /y
xcopy %CILOGG_WORKSPACE%\packaging\windows\FileAssociation.nsh  /y

echo "Making portable archive..."
pushd %CILOGG_WORKSPACE%
7z a -r %CILOGG_WORKSPACE%\cilogg-%CILOGG_VERSION%-%CILOGG_ARCH%-%CILOGG_QT%-portable.zip .\release\* -xr!*.pdb
popd

set "PDB_ARCHIVE=%CILOGG_WORKSPACE%\cilogg-%CILOGG_VERSION%-%CILOGG_ARCH%-%CILOGG_QT%-pdb.zip"
set "PDB_LIST=%CILOGG_WORKSPACE%\release\pdb_file_list.txt"
if exist "%PDB_LIST%" del "%PDB_LIST%"
for /f "delims=" %%F in ('dir /b /a-d "%CILOGG_WORKSPACE%\release\*.pdb" 2^>nul') do echo %CILOGG_WORKSPACE%\release\%%F>>"%PDB_LIST%"

if exist "%PDB_LIST%" (
  7z a "%PDB_ARCHIVE%" @"%PDB_LIST%"
) else (
  echo No PDB files were produced for this build>"%CILOGG_WORKSPACE%\release\NO_PDB_SYMBOLS.txt"
  7z a "%PDB_ARCHIVE%" "%CILOGG_WORKSPACE%\release\NO_PDB_SYMBOLS.txt"
  del "%CILOGG_WORKSPACE%\release\NO_PDB_SYMBOLS.txt"
)

if exist "%PDB_LIST%" del "%PDB_LIST%"

echo "Done!"
goto :eof

:manual_qt_deploy
echo "Using manual Qt deploy for %CILOGG_ARCH%"
xcopy %QTDIR%\bin\Qt6*.dll %CILOGG_WORKSPACE%\release\ /y
xcopy %QTDIR%\bin\icu*.dll %CILOGG_WORKSPACE%\release\ /y
xcopy %QTDIR%\bin\D3Dcompiler_47.dll %CILOGG_WORKSPACE%\release\ /y
xcopy %QTDIR%\bin\dxcompiler.dll %CILOGG_WORKSPACE%\release\ /y
xcopy %QTDIR%\bin\dxil.dll %CILOGG_WORKSPACE%\release\ /y

if exist %QTDIR%\plugins\platforms xcopy %QTDIR%\plugins\platforms %CILOGG_WORKSPACE%\release\platforms\ /e /i /y
if exist %QTDIR%\plugins\generic xcopy %QTDIR%\plugins\generic %CILOGG_WORKSPACE%\release\generic\ /e /i /y
if exist %QTDIR%\plugins\iconengines xcopy %QTDIR%\plugins\iconengines %CILOGG_WORKSPACE%\release\iconengines\ /e /i /y
if exist %QTDIR%\plugins\imageformats xcopy %QTDIR%\plugins\imageformats %CILOGG_WORKSPACE%\release\imageformats\ /e /i /y
if exist %QTDIR%\plugins\networkinformation xcopy %QTDIR%\plugins\networkinformation %CILOGG_WORKSPACE%\release\networkinformation\ /e /i /y
if exist %QTDIR%\plugins\sqldrivers xcopy %QTDIR%\plugins\sqldrivers %CILOGG_WORKSPACE%\release\sqldrivers\ /e /i /y
if exist %QTDIR%\plugins\styles xcopy %QTDIR%\plugins\styles %CILOGG_WORKSPACE%\release\styles\ /e /i /y
if exist %QTDIR%\plugins\tls xcopy %QTDIR%\plugins\tls %CILOGG_WORKSPACE%\release\tls\ /e /i /y
if exist %QTDIR%\translations xcopy %QTDIR%\translations %CILOGG_WORKSPACE%\release\translations\ /e /i /y
exit /b 0
