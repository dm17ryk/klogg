echo %KLOGG_QT%
echo %KLOGG_QT_DIR%

md %KLOGG_WORKSPACE%\release

echo "Copying klogg binaries..."
xcopy %KLOGG_WORKSPACE%\%KLOGG_BUILD_ROOT%\output\klogg_portable.exe %KLOGG_WORKSPACE%\release\ /y
xcopy %KLOGG_WORKSPACE%\%KLOGG_BUILD_ROOT%\output\klogg_portable.pdb %KLOGG_WORKSPACE%\release\ /y
xcopy %KLOGG_WORKSPACE%\%KLOGG_BUILD_ROOT%\output\klogg.exe %KLOGG_WORKSPACE%\release\ /y
xcopy %KLOGG_WORKSPACE%\%KLOGG_BUILD_ROOT%\output\klogg.pdb %KLOGG_WORKSPACE%\release\ /y

xcopy %KLOGG_WORKSPACE%\%KLOGG_BUILD_ROOT%\output\klogg_crashpad_handler.exe %KLOGG_WORKSPACE%\release\ /y
xcopy %KLOGG_WORKSPACE%\%KLOGG_BUILD_ROOT%\output\klogg_minidump_dump.exe %KLOGG_WORKSPACE%\release\ /y

xcopy %KLOGG_WORKSPACE%\%KLOGG_BUILD_ROOT%\msvc_19.41_cxx17_64_md_relwithdebinfo\tbb12.dll %KLOGG_WORKSPACE%\release\ /y
xcopy %KLOGG_WORKSPACE%\%KLOGG_BUILD_ROOT%\msvc_19.41_cxx17_64_md_relwithdebinfo\tbb12.pdb %KLOGG_WORKSPACE%\release\ /y
xcopy %KLOGG_WORKSPACE%\%KLOGG_BUILD_ROOT%\msvc_19.41_cxx17_32_md_relwithdebinfo\tbb12.dll %KLOGG_WORKSPACE%\release\ /y
xcopy %KLOGG_WORKSPACE%\%KLOGG_BUILD_ROOT%\msvc_19.41_cxx17_32_md_relwithdebinfo\tbb12.pdb %KLOGG_WORKSPACE%\release\ /y

xcopy %KLOGG_WORKSPACE%\%KLOGG_BUILD_ROOT%\msvc_19.42_cxx17_64_md_relwithdebinfo\tbb12.dll %KLOGG_WORKSPACE%\release\ /y
xcopy %KLOGG_WORKSPACE%\%KLOGG_BUILD_ROOT%\msvc_19.42_cxx17_64_md_relwithdebinfo\tbb12.pdb %KLOGG_WORKSPACE%\release\ /y
xcopy %KLOGG_WORKSPACE%\%KLOGG_BUILD_ROOT%\msvc_19.42_cxx17_32_md_relwithdebinfo\tbb12.dll %KLOGG_WORKSPACE%\release\ /y
xcopy %KLOGG_WORKSPACE%\%KLOGG_BUILD_ROOT%\msvc_19.42_cxx17_32_md_relwithdebinfo\tbb12.pdb %KLOGG_WORKSPACE%\release\ /y

xcopy %KLOGG_WORKSPACE%\%KLOGG_BUILD_ROOT%\msvc_19.42_cxx17_64_md_relwithdebinfo\tbbmalloc.dll %KLOGG_WORKSPACE%\release\ /y
xcopy %KLOGG_WORKSPACE%\%KLOGG_BUILD_ROOT%\msvc_19.42_cxx17_64_md_relwithdebinfo\tbbmalloc.pdb %KLOGG_WORKSPACE%\release\ /y
xcopy %KLOGG_WORKSPACE%\%KLOGG_BUILD_ROOT%\msvc_19.42_cxx17_32_md_relwithdebinfo\tbbmalloc.dll %KLOGG_WORKSPACE%\release\ /y
xcopy %KLOGG_WORKSPACE%\%KLOGG_BUILD_ROOT%\msvc_19.42_cxx17_32_md_relwithdebinfo\tbbmalloc.pdb %KLOGG_WORKSPACE%\release\ /y

xcopy %KLOGG_WORKSPACE%\%KLOGG_BUILD_ROOT%\msvc_19.42_cxx17_64_md_relwithdebinfo\tbbmalloc_proxy.dll %KLOGG_WORKSPACE%\release\ /y
xcopy %KLOGG_WORKSPACE%\%KLOGG_BUILD_ROOT%\msvc_19.42_cxx17_64_md_relwithdebinfo\tbbmalloc_proxy.pdb %KLOGG_WORKSPACE%\release\ /y
xcopy %KLOGG_WORKSPACE%\%KLOGG_BUILD_ROOT%\msvc_19.42_cxx17_32_md_relwithdebinfo\tbbmalloc_proxy.dll %KLOGG_WORKSPACE%\release\ /y
xcopy %KLOGG_WORKSPACE%\%KLOGG_BUILD_ROOT%\msvc_19.42_cxx17_32_md_relwithdebinfo\tbbmalloc_proxy.pdb %KLOGG_WORKSPACE%\release\ /y

xcopy %KLOGG_WORKSPACE%\%KLOGG_BUILD_ROOT%\msvc_19.44_cxx17_64_md_relwithdebinfo\tbb12.dll %KLOGG_WORKSPACE%\release\ /y
xcopy %KLOGG_WORKSPACE%\%KLOGG_BUILD_ROOT%\msvc_19.44_cxx17_64_md_relwithdebinfo\tbb12.pdb %KLOGG_WORKSPACE%\release\ /y
xcopy %KLOGG_WORKSPACE%\%KLOGG_BUILD_ROOT%\msvc_19.44_cxx17_32_md_relwithdebinfo\tbb12.dll %KLOGG_WORKSPACE%\release\ /y
xcopy %KLOGG_WORKSPACE%\%KLOGG_BUILD_ROOT%\msvc_19.44_cxx17_32_md_relwithdebinfo\tbb12.pdb %KLOGG_WORKSPACE%\release\ /y

xcopy %KLOGG_WORKSPACE%\%KLOGG_BUILD_ROOT%\msvc_19.44_cxx17_64_md_relwithdebinfo\tbbmalloc.dll %KLOGG_WORKSPACE%\release\ /y
xcopy %KLOGG_WORKSPACE%\%KLOGG_BUILD_ROOT%\msvc_19.44_cxx17_64_md_relwithdebinfo\tbbmalloc.pdb %KLOGG_WORKSPACE%\release\ /y
xcopy %KLOGG_WORKSPACE%\%KLOGG_BUILD_ROOT%\msvc_19.44_cxx17_32_md_relwithdebinfo\tbbmalloc.dll %KLOGG_WORKSPACE%\release\ /y
xcopy %KLOGG_WORKSPACE%\%KLOGG_BUILD_ROOT%\msvc_19.44_cxx17_32_md_relwithdebinfo\tbbmalloc.pdb %KLOGG_WORKSPACE%\release\ /y

xcopy %KLOGG_WORKSPACE%\%KLOGG_BUILD_ROOT%\msvc_19.44_cxx17_64_md_relwithdebinfo\tbbmalloc_proxy.dll %KLOGG_WORKSPACE%\release\ /y
xcopy %KLOGG_WORKSPACE%\%KLOGG_BUILD_ROOT%\msvc_19.44_cxx17_64_md_relwithdebinfo\tbbmalloc_proxy.pdb %KLOGG_WORKSPACE%\release\ /y
xcopy %KLOGG_WORKSPACE%\%KLOGG_BUILD_ROOT%\msvc_19.44_cxx17_32_md_relwithdebinfo\tbbmalloc_proxy.dll %KLOGG_WORKSPACE%\release\ /y
xcopy %KLOGG_WORKSPACE%\%KLOGG_BUILD_ROOT%\msvc_19.44_cxx17_32_md_relwithdebinfo\tbbmalloc_proxy.pdb %KLOGG_WORKSPACE%\release\ /y

xcopy %KLOGG_WORKSPACE%\%KLOGG_BUILD_ROOT%\msvc_19.44_cxx17_64_md_release\tbb12.dll %KLOGG_WORKSPACE%\release\ /y
xcopy %KLOGG_WORKSPACE%\%KLOGG_BUILD_ROOT%\msvc_19.44_cxx17_64_md_release\tbb12.pdb %KLOGG_WORKSPACE%\release\ /y
xcopy %KLOGG_WORKSPACE%\%KLOGG_BUILD_ROOT%\msvc_19.44_cxx17_32_md_release\tbb12.dll %KLOGG_WORKSPACE%\release\ /y
xcopy %KLOGG_WORKSPACE%\%KLOGG_BUILD_ROOT%\msvc_19.44_cxx17_32_md_release\tbb12.pdb %KLOGG_WORKSPACE%\release\ /y

xcopy %KLOGG_WORKSPACE%\%KLOGG_BUILD_ROOT%\msvc_19.44_cxx17_64_md_release\tbbmalloc.dll %KLOGG_WORKSPACE%\release\ /y
xcopy %KLOGG_WORKSPACE%\%KLOGG_BUILD_ROOT%\msvc_19.44_cxx17_64_md_release\tbbmalloc.pdb %KLOGG_WORKSPACE%\release\ /y
xcopy %KLOGG_WORKSPACE%\%KLOGG_BUILD_ROOT%\msvc_19.44_cxx17_32_md_release\tbbmalloc.dll %KLOGG_WORKSPACE%\release\ /y
xcopy %KLOGG_WORKSPACE%\%KLOGG_BUILD_ROOT%\msvc_19.44_cxx17_32_md_release\tbbmalloc.pdb %KLOGG_WORKSPACE%\release\ /y

xcopy %KLOGG_WORKSPACE%\%KLOGG_BUILD_ROOT%\msvc_19.44_cxx17_64_md_release\tbbmalloc_proxy.dll %KLOGG_WORKSPACE%\release\ /y
xcopy %KLOGG_WORKSPACE%\%KLOGG_BUILD_ROOT%\msvc_19.44_cxx17_64_md_release\tbbmalloc_proxy.pdb %KLOGG_WORKSPACE%\release\ /y
xcopy %KLOGG_WORKSPACE%\%KLOGG_BUILD_ROOT%\msvc_19.44_cxx17_32_md_release\tbbmalloc_proxy.dll %KLOGG_WORKSPACE%\release\ /y
xcopy %KLOGG_WORKSPACE%\%KLOGG_BUILD_ROOT%\msvc_19.44_cxx17_32_md_release\tbbmalloc_proxy.pdb %KLOGG_WORKSPACE%\release\ /y

xcopy %KLOGG_WORKSPACE%\%KLOGG_BUILD_ROOT%\generated\documentation.html %KLOGG_WORKSPACE%\release\ /y
xcopy %KLOGG_WORKSPACE%\COPYING %KLOGG_WORKSPACE%\release\ /y
xcopy %KLOGG_WORKSPACE%\NOTICE %KLOGG_WORKSPACE%\release\ /y
xcopy %KLOGG_WORKSPACE%\README.md %KLOGG_WORKSPACE%\release\ /y
xcopy %KLOGG_WORKSPACE%\DOCUMENTATION.md %KLOGG_WORKSPACE%\release\ /y
xcopy %KLOGG_WORKSPACE%\%KLOGG_BUILD_ROOT%\output\python_runtime %KLOGG_WORKSPACE%\release\python_runtime\ /e /i /y

echo "Copying vc runtime..."
xcopy "%VCToolsRedistDir%%platform%\Microsoft.VC143.CRT\msvcp140.dll" %KLOGG_WORKSPACE%\release\ /y
xcopy "%VCToolsRedistDir%%platform%\Microsoft.VC143.CRT\msvcp140_1.dll" %KLOGG_WORKSPACE%\release\ /y
xcopy "%VCToolsRedistDir%%platform%\Microsoft.VC143.CRT\msvcp140_2.dll" %KLOGG_WORKSPACE%\release\ /y
xcopy "%VCToolsRedistDir%%platform%\Microsoft.VC143.CRT\vcruntime140.dll" %KLOGG_WORKSPACE%\release\ /y
xcopy "%VCToolsRedistDir%%platform%\Microsoft.VC143.CRT\vcruntime140_1.dll" %KLOGG_WORKSPACE%\release\ /y

echo "Copying ssl..."
xcopy %SSL_DIR%\libcrypto-1_1%SSL_ARCH%.dll %KLOGG_WORKSPACE%\release\ /y
xcopy %SSL_DIR%\libssl-1_1%SSL_ARCH%.dll %KLOGG_WORKSPACE%\release\ /y

echo "Copying Qt..."
if "%KLOGG_QT_DIR%"=="" (
  echo ERROR: KLOGG_QT_DIR is empty
  exit /b 1
)
set "QTDIR=%KLOGG_QT_DIR:/=\%"
echo %QTDIR%
xcopy %QTDIR%\bin\%KLOGG_QT%Core.dll %KLOGG_WORKSPACE%\release\ /y
xcopy %QTDIR%\bin\%KLOGG_QT%Gui.dll %KLOGG_WORKSPACE%\release\ /y
xcopy %QTDIR%\bin\%KLOGG_QT%Network.dll %KLOGG_WORKSPACE%\release\ /y
xcopy %QTDIR%\bin\%KLOGG_QT%Widgets.dll %KLOGG_WORKSPACE%\release\ /y
xcopy %QTDIR%\bin\%KLOGG_QT%Concurrent.dll %KLOGG_WORKSPACE%\release\ /y
xcopy %QTDIR%\bin\%KLOGG_QT%Xml.dll %KLOGG_WORKSPACE%\release\ /y
xcopy %QTDIR%\bin\%KLOGG_QT%Core5Compat.dll %KLOGG_WORKSPACE%\release\ /y
xcopy %QTDIR%\bin\%KLOGG_QT%SerialPort.dll %KLOGG_WORKSPACE%\release\ /y
xcopy %QTDIR%\bin\%KLOGG_QT%Svg.dll %KLOGG_WORKSPACE%\release\ /y

echo "Deploying Qt runtime..."
if /i "%KLOGG_ARCH%"=="arm64" (
  call :manual_qt_deploy
  if errorlevel 1 exit /b 1
) else (
  "%QTDIR%\bin\windeployqt.exe" --force --no-compiler-runtime --dir %KLOGG_WORKSPACE%\release %KLOGG_WORKSPACE%\release\klogg.exe
  if errorlevel 1 exit /b 1
)

echo "Copying packaging files..."
md %KLOGG_WORKSPACE%\chocolately
xcopy %KLOGG_WORKSPACE%\packaging\windows\klogg.nuspec chocolately /y

md %KLOGG_WORKSPACE%\chocolately\tools
xcopy %KLOGG_WORKSPACE%\packaging\windows\chocolatelyInstall.ps1 chocolately\tools\ /y

xcopy %KLOGG_WORKSPACE%\packaging\windows\klogg.nsi  /y
xcopy %KLOGG_WORKSPACE%\packaging\windows\FileAssociation.nsh  /y

echo "Making portable archive..."
pushd %KLOGG_WORKSPACE%
7z a -r %KLOGG_WORKSPACE%\klogg-%KLOGG_VERSION%-%KLOGG_ARCH%-%KLOGG_QT%-portable.zip .\release\* -xr!*.pdb
popd

set "PDB_ARCHIVE=%KLOGG_WORKSPACE%\klogg-%KLOGG_VERSION%-%KLOGG_ARCH%-%KLOGG_QT%-pdb.zip"
set "PDB_LIST=%KLOGG_WORKSPACE%\release\pdb_file_list.txt"
if exist "%PDB_LIST%" del "%PDB_LIST%"
for /f "delims=" %%F in ('dir /b /a-d "%KLOGG_WORKSPACE%\release\*.pdb" 2^>nul') do echo %KLOGG_WORKSPACE%\release\%%F>>"%PDB_LIST%"

if exist "%PDB_LIST%" (
  7z a "%PDB_ARCHIVE%" @"%PDB_LIST%"
) else (
  echo No PDB files were produced for this build>"%KLOGG_WORKSPACE%\release\NO_PDB_SYMBOLS.txt"
  7z a "%PDB_ARCHIVE%" "%KLOGG_WORKSPACE%\release\NO_PDB_SYMBOLS.txt"
  del "%KLOGG_WORKSPACE%\release\NO_PDB_SYMBOLS.txt"
)

if exist "%PDB_LIST%" del "%PDB_LIST%"

echo "Done!"
goto :eof

:manual_qt_deploy
echo "Using manual Qt deploy for %KLOGG_ARCH%"
xcopy %QTDIR%\bin\Qt6*.dll %KLOGG_WORKSPACE%\release\ /y
xcopy %QTDIR%\bin\icu*.dll %KLOGG_WORKSPACE%\release\ /y
xcopy %QTDIR%\bin\D3Dcompiler_47.dll %KLOGG_WORKSPACE%\release\ /y
xcopy %QTDIR%\bin\dxcompiler.dll %KLOGG_WORKSPACE%\release\ /y
xcopy %QTDIR%\bin\dxil.dll %KLOGG_WORKSPACE%\release\ /y

if exist %QTDIR%\plugins\platforms xcopy %QTDIR%\plugins\platforms %KLOGG_WORKSPACE%\release\platforms\ /e /i /y
if exist %QTDIR%\plugins\generic xcopy %QTDIR%\plugins\generic %KLOGG_WORKSPACE%\release\generic\ /e /i /y
if exist %QTDIR%\plugins\iconengines xcopy %QTDIR%\plugins\iconengines %KLOGG_WORKSPACE%\release\iconengines\ /e /i /y
if exist %QTDIR%\plugins\imageformats xcopy %QTDIR%\plugins\imageformats %KLOGG_WORKSPACE%\release\imageformats\ /e /i /y
if exist %QTDIR%\plugins\networkinformation xcopy %QTDIR%\plugins\networkinformation %KLOGG_WORKSPACE%\release\networkinformation\ /e /i /y
if exist %QTDIR%\plugins\sqldrivers xcopy %QTDIR%\plugins\sqldrivers %KLOGG_WORKSPACE%\release\sqldrivers\ /e /i /y
if exist %QTDIR%\plugins\styles xcopy %QTDIR%\plugins\styles %KLOGG_WORKSPACE%\release\styles\ /e /i /y
if exist %QTDIR%\plugins\tls xcopy %QTDIR%\plugins\tls %KLOGG_WORKSPACE%\release\tls\ /e /i /y
if exist %QTDIR%\translations xcopy %QTDIR%\translations %KLOGG_WORKSPACE%\release\translations\ /e /i /y
exit /b 0
