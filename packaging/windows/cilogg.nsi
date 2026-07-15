# NSIS script creating the Windows installer for cilogg

# Is passed to the script using -DVERSION=$(git describe) on the command line
!ifndef VERSION
    !define VERSION 'dev-build'
!endif

!ifndef PLATFORM
    !define PLATFORM 'unknown'
!endif

!ifndef QT_MAJOR
    !define QT_MAJOR 'Qt5'
!endif

# Headers
!include "MUI2.nsh"
!include "FileAssociation.nsh"

# General
OutFile "cilogg-${VERSION}-${PLATFORM}-${QT_MAJOR}-setup.exe"

XpStyle on

SetCompressor /SOLID lzma

; Registry key to keep track of the directory we are installed in
!ifdef ARCH32
  InstallDir "$PROGRAMFILES\cilogg"
!else
  InstallDir "$PROGRAMFILES64\cilogg"
!endif
InstallDirRegKey HKLM Software\cilogg ""

; cilogg icon
; !define MUI_ICON cilogg.ico

RequestExecutionLevel admin

Name "CILogg"
Caption "CILogg ${VERSION} Setup"

# Pages
!define MUI_WELCOMEPAGE_TITLE "Welcome to the CILogg ${VERSION} Setup Wizard"
!define MUI_WELCOMEPAGE_TEXT "This wizard will guide you through the installation of CILogg\
, a fast, advanced log explorer.$\r$\n$\r$\n\
CILogg and the Qt libraries are released under the GPL, see \
the COPYING and NOTICE files.$\r$\n$\r$\n$_CLICK"
;MUI_FINISHPAGE_LINK_LOCATION "https://github.com/dm17ryk/klogg"

!insertmacro MUI_PAGE_WELCOME
;!insertmacro MUI_PAGE_LICENSE "COPYING"
# !ifdef VER_MAJOR & VER_MINOR & VER_REVISION & VER_BUILD...
!insertmacro MUI_PAGE_COMPONENTS
!insertmacro MUI_PAGE_DIRECTORY
!insertmacro MUI_PAGE_INSTFILES
!insertmacro MUI_PAGE_FINISH

!insertmacro MUI_UNPAGE_WELCOME
!insertmacro MUI_UNPAGE_CONFIRM
!insertmacro MUI_UNPAGE_INSTFILES
!insertmacro MUI_UNPAGE_FINISH

# Languages
!insertmacro MUI_LANGUAGE "English"

# Installer sections
Section "CILogg" cilogg
    ; Prevent this section from being unselected
    SectionIn RO

    SetOutPath $INSTDIR
File release\cilogg.exe
File release\cilogg_updater.exe
    File /nonfatal release\cilogg_crashpad_handler.exe
    File /nonfatal release\cilogg_minidump_dump.exe
    File /nonfatal release\tbb12.dll

    File COPYING
    File NOTICE
    File README.md
    File DOCUMENTATION.md
    File release\documentation.html
    SetOutPath $INSTDIR\python_runtime
    File /nonfatal /r release\python_runtime\*

    ; Create the 'sendto' link
    CreateShortCut "$SENDTO\cilogg.lnk" "$INSTDIR\cilogg.exe" "" "$INSTDIR\cilogg.exe" 0

    ; Register as an otion (but not main handler) for some files (.txt, .Log, .cap)
    WriteRegStr HKCR "Applications\cilogg.exe" "" ""
    WriteRegStr HKCR "Applications\cilogg.exe\shell" "" "open"
    WriteRegStr HKCR "Applications\cilogg.exe\shell\open" "CILogg log viewer" "cilogg"
    WriteRegStr HKCR "Applications\cilogg.exe\shell\open\command" "" '"$INSTDIR\cilogg.exe" "%1"'
    WriteRegStr HKCR "*\OpenWithList\cilogg.exe" "" ""
    WriteRegStr HKCR ".txt\OpenWithList\cilogg.exe" "" ""
    WriteRegStr HKCR ".Log\OpenWithList\cilogg.exe" "" ""
    WriteRegStr HKCR ".cap\OpenWithList\cilogg.exe" "" ""

    ; Register uninstaller
    WriteRegExpandStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\cilogg"\
"UninstallString" '"$INSTDIR\Uninstall.exe"'
    WriteRegExpandStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\cilogg"\
"InstallLocation" "$INSTDIR"
    WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\cilogg" "DisplayName" "CILogg"
    WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\cilogg" "DisplayVersion" "${VERSION}"
    WriteRegDWORD HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\cilogg" "NoModify" "1"
    WriteRegDWORD HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\cilogg" "NoRepair" "1"

    ; Create uninstaller
    WriteUninstaller "$INSTDIR\Uninstall.exe"
SectionEnd

Section "Qt Runtime libraries" qtlibs
    SetOutPath $INSTDIR
    File /nonfatal release\Qt*.dll
    File /nonfatal release\icu*.dll
    File /nonfatal release\D3Dcompiler_47.dll
    File /nonfatal release\dxcompiler.dll
    File /nonfatal release\dxil.dll

    SetOutPath $INSTDIR\platforms
    File /nonfatal /r release\platforms\*
    SetOutPath $INSTDIR\generic
    File /nonfatal /r release\generic\*
    SetOutPath $INSTDIR\iconengines
    File /nonfatal /r release\iconengines\*
    SetOutPath $INSTDIR\imageformats
    File /nonfatal /r release\imageformats\*
    SetOutPath $INSTDIR\networkinformation
    File /nonfatal /r release\networkinformation\*
    SetOutPath $INSTDIR\sqldrivers
    File /nonfatal /r release\sqldrivers\*
    SetOutPath $INSTDIR\styles
    File /nonfatal /r release\styles\*
    SetOutPath $INSTDIR\tls
    File /nonfatal /r release\tls\*
    SetOutPath $INSTDIR\translations
    File /nonfatal /r release\translations\*

SectionEnd

Section "MSVC Runtime libraries" vcruntime
    SetOutPath $INSTDIR
    File release\msvcp140.dll
    File release\msvcp140_1.dll
    File release\vcruntime140.dll
    
!if ${PLATFORM} == "x64"
    File release\vcruntime140_1.dll

    File /nonfatal release\libcrypto-1_1-x64.dll
    File /nonfatal release\libssl-1_1-x64.dll
!else
    File /nonfatal release\libcrypto-1_1.dll
    File /nonfatal release\libssl-1_1.dll
!endif

SectionEnd

Section "Create Start menu shortcut" shortcut
    SetShellVarContext all
    CreateShortCut "$SMPROGRAMS\cilogg.lnk" "$INSTDIR\cilogg.exe" "" "$INSTDIR\cilogg.exe" 0
SectionEnd

Section /o "Associate with .log files" associate
    ${registerExtension} "$INSTDIR\cilogg.exe" ".log" "Log file"
SectionEnd

# Descriptions
!insertmacro MUI_FUNCTION_DESCRIPTION_BEGIN
    !insertmacro MUI_DESCRIPTION_TEXT ${cilogg} "The core files required to use CILogg."
    !insertmacro MUI_DESCRIPTION_TEXT ${qtlibs} "Needed by CILogg, you have to install these unless \
you already have the Qt development kit installed."
    !insertmacro MUI_DESCRIPTION_TEXT ${vcruntime} "Needed by CILogg, you have to install these unless \
you already have the Microsoft Visual C++ 2017 Redistributable installed."
    !insertmacro MUI_DESCRIPTION_TEXT ${shortcut} "Create a shortcut in the Start menu for CILogg."
    !insertmacro MUI_DESCRIPTION_TEXT ${associate} "Make CILogg the default viewer for .log files."
!insertmacro MUI_FUNCTION_DESCRIPTION_END

# Uninstaller
Section "Uninstall"
    Delete "$INSTDIR\Uninstall.exe"

    Delete "$INSTDIR\cilogg.exe"
    Delete "$INSTDIR\cilogg_crashpad_handler.exe"
    Delete "$INSTDIR\cilogg_minidump_dump.exe"
    Delete "$INSTDIR\README.md"
    Delete "$INSTDIR\COPYING"
    Delete "$INSTDIR\NOTICE"
    Delete "$INSTDIR\readme.html"
    Delete "$INSTDIR\documentation.md"
    Delete "$INSTDIR\documentation.html"
    Delete "$INSTDIR\libstdc++-6.dll"
    Delete "$INSTDIR\libgcc_s_seh-1.dll"
    Delete "$INSTDIR\libgcc_s_dw2-1.dll"
    Delete "$INSTDIR\Qt5Widgets.dll"
    Delete "$INSTDIR\Qt5Core.dll"
    Delete "$INSTDIR\Qt5Gui.dll"
    Delete "$INSTDIR\Qt5Network.dll"
    Delete "$INSTDIR\Qt5Concurrent.dll"
    Delete "$INSTDIR\Qt5Xml.dll"
    Delete "$INSTDIR\Qt6Widgets.dll"
    Delete "$INSTDIR\Qt6Core.dll"
    Delete "$INSTDIR\Qt6Gui.dll"
    Delete "$INSTDIR\Qt6Network.dll"
    Delete "$INSTDIR\Qt6Concurrent.dll"
    Delete "$INSTDIR\Qt6Xml.dll"
    Delete "$INSTDIR\Qt6Core5Compat.dll"
    Delete "$INSTDIR\Qt6SerialPort.dll"
    Delete "$INSTDIR\Qt6Svg.dll"
    Delete "$INSTDIR\platforms\qwindows.dll"
    Delete "$INSTDIR\platforms\qminimal.dll"
    Delete "$INSTDIR\styles\qwindowsvistastyle.dll"
    Delete "$INSTDIR\msvcp140.dll"
    Delete "$INSTDIR\msvcp140_1.dll"
    Delete "$INSTDIR\vcruntime140.dll"
    Delete "$INSTDIR\vcruntime140_1.dll"
    Delete "$INSTDIR\tbb12.dll"
    Delete "$INSTDIR\tbbmalloc.dll"
    Delete "$INSTDIR\tbbmalloc_proxy.dll"
    Delete "$INSTDIR\cilogg_tbbmalloc.dll"
    Delete "$INSTDIR\cilogg_tbbmalloc_proxy.dll"
    Delete "$INSTDIR\libcrypto-1_1-x64.dll"
    Delete "$INSTDIR\libssl-1_1-x64.dll"
    Delete "$INSTDIR\libcrypto-1_1.dll"
    Delete "$INSTDIR\libssl-1_1.dll"
    Delete "$INSTDIR\mimalloc.dll"
    Delete "$INSTDIR\mimalloc_override.dll"
    Delete "$INSTDIR\mimalloc_redirect.dll"
    Delete "$INSTDIR\mimalloc_redirect32.dll"
    Delete "$INSTDIR\mimalloc-redirect.dll"
    Delete "$INSTDIR\mimalloc-redirect32.dll"
    Delete "$INSTDIR\*.dll"
    RMDir /r "$INSTDIR\python_runtime"
    RMDir /r "$INSTDIR\platforms"
    RMDir /r "$INSTDIR\generic"
    RMDir /r "$INSTDIR\iconengines"
    RMDir /r "$INSTDIR\imageformats"
    RMDir /r "$INSTDIR\networkinformation"
    RMDir /r "$INSTDIR\sqldrivers"
    RMDir /r "$INSTDIR\styles"
    RMDir /r "$INSTDIR\tls"
    RMDir /r "$INSTDIR\translations"
    RMDir "$INSTDIR"

    ; Remove settings in %appdata%
    Delete "$APPDATA\cilogg\cilogg.ini"
    RMDir "$APPDATA\cilogg"

    DeleteRegKey HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\cilogg"

    ; Remove the file associations
    ${unregisterExtension} ".log" "Log file"

    DeleteRegKey HKCR "*\OpenWithList\cilogg.exe"
    DeleteRegKey HKCR ".txt\OpenWithList\cilogg.exe"
    DeleteRegKey HKCR ".Log\OpenWithList\cilogg.exe"
    DeleteRegKey HKCR ".cap\OpenWithList\cilogg.exe"
    DeleteRegKey HKCR "Applications\cilogg.exe\shell\open\command"
    DeleteRegKey HKCR "Applications\cilogg.exe\shell\open"
    DeleteRegKey HKCR "Applications\cilogg.exe\shell"
    DeleteRegKey HKCR "Applications\cilogg.exe"

    ; Remove the shortcut, if any
    SetShellVarContext all
    Delete "$SMPROGRAMS\cilogg.lnk"
SectionEnd

;!uninstfinalize 'packaging\windows\codesign_client.exe --debug "%1"'
