# NSIS script creating the Windows installer for cilogg

Unicode true

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
; Next to this script, also when it is compiled with /NOCD from the package folder.
!include "${__FILEDIR__}\FileAssociation.nsh"
!include "LogicLib.nsh"
!include "FileFunc.nsh"

# General
!ifdef OUTFILE
OutFile "${OUTFILE}"
!else
OutFile "cilogg-${VERSION}-${PLATFORM}-${QT_MAJOR}-setup.exe"
!endif

XpStyle on

SetCompressor /SOLID lzma

; Reuse the location written by previous installers, including custom folders.
!ifdef ARCH32
  InstallDir "$PROGRAMFILES\cilogg"
!else
  InstallDir "$PROGRAMFILES64\cilogg"
!endif
InstallDirRegKey HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\cilogg" "InstallLocation"

; cilogg icon
; !define MUI_ICON cilogg.ico

!ifdef CILOGG_TEST_INSTALL
; Test builds (tests/packaging/windows_installer_test.ps1) install into a temporary folder
; without administrator rights and leave the registry, Start menu and Send To alone.
RequestExecutionLevel user
!else
RequestExecutionLevel admin
!endif

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

; Installer log (UTF-16LE) in the installation folder: the update helper runs setup silently,
; which discards DetailPrint, and copies this log next to its own. Like the report, it is written
; into $INSTDIR, where setup copies its files anyway, never into the user's profile.
Var CiloggLog
; The files that blocked the installation and the processes using them.
Var CiloggReport
; 1 once files in $INSTDIR were moved aside: a failure from then on puts them back.
Var CiloggPrepared
; 1 when a new file could not be written.
Var CiloggCopyFailed
; 1 when the previous files could not all be put back after a failure.
Var CiloggRestoreFailed
; $INSTDIR as a command-line argument: "C:\." for a drive root, whose trailing backslash would
; escape the closing quote.
Var CiloggDir

Function CiloggWriteLog
    Exch $0
    Push $1
    Push $2
    DetailPrint "$0"
    ${If} $CiloggLog != ""
        FileOpen $1 "$CiloggLog" a
        ${If} $1 != ""
            FileSeek $1 0 END $2
            ${If} $2 == 0
                FileWriteWord $1 0xFEFF
            ${EndIf}
            FileWriteUTF16LE $1 "$0$\r$\n"
            FileClose $1
        ${EndIf}
    ${EndIf}
    Pop $2
    Pop $1
    Pop $0
FunctionEnd

!macro CiloggLog text
    Push "${text}"
    Call CiloggWriteLog
!macroend
!define CiloggLog "!insertmacro CiloggLog"

Function CiloggStartLog
    CreateDirectory "$INSTDIR"
    ; Names shared with cilogg_updater (update_protocol::InstallerLogFileName and
    ; InstallerReportFileName).
    StrCpy $CiloggLog "$INSTDIR\cilogg-installer.log"
    StrCpy $CiloggReport "$INSTDIR\cilogg-installer-report.txt"
    Delete "$CiloggLog"
    Delete "$CiloggReport"
    StrCpy $CiloggDir "$INSTDIR"
    StrCpy $0 "$INSTDIR" 1 -1
    ${If} $0 == "\"
        StrCpy $CiloggDir "$INSTDIR."
    ${EndIf}
    ${GetTime} "" "L" $0 $1 $2 $3 $4 $5 $6
    ${CiloggLog} "CILogg ${VERSION} setup started $2-$1-$0 $4:$5:$6"
FunctionEnd

; Running CILogg processes are not closed: Windows lets a running executable or a loaded DLL be
; renamed, so the helper moves the files that may be in use aside (*.cilogg-old) and the new
; ones are copied in. MCP servers started by AI agents, CLI jobs and other windows keep their
; old copy; nothing outside $INSTDIR is touched. The helper is this installer's own copy, run
; from its temporary folder.
Function CiloggPrepareInstallDirectory
    Call CiloggStartLog
    ${CiloggLog} "CILogg installation directory: $INSTDIR"
    InitPluginsDir
    File "/oname=$PLUGINSDIR\cilogg_updater.exe" release\cilogg_updater.exe
    nsExec::ExecToLog '"$PLUGINSDIR\cilogg_updater.exe" --installer-prepare "$CiloggDir" --log "$CiloggLog" --report "$CiloggReport"'
    Pop $0
    ${If} $0 == 0
        StrCpy $CiloggPrepared 1
        Return
    ${EndIf}
    ${If} $0 == 2
        ; Exit codes: update_protocol::Installer*ExitCode.
        ${CiloggLog} "Setup cancelled before replacing files: files in $INSTDIR are in use and could not be moved aside"
        SetErrorLevel 2
        Abort "Some CILogg files are in use and cannot be replaced. Installation cancelled before replacing files."
    ${EndIf}
    ${If} $0 == 3
        ; Files were moved aside and not all of them could be put back: try once more.
        StrCpy $CiloggPrepared 1
        Call CiloggRollback
        ${If} $CiloggRestoreFailed == 1
            SetErrorLevel 4
        ${Else}
            SetErrorLevel 2
        ${EndIf}
        Abort "Some CILogg files are in use and cannot be replaced. Installation cancelled."
    ${EndIf}
    ${If} $0 == "error"
        ${If} ${FileExists} "$PLUGINSDIR\cilogg_updater.exe"
            ${CiloggLog} "Setup could not start $PLUGINSDIR\cilogg_updater.exe"
        ${Else}
            ${CiloggLog} "Setup could not unpack $PLUGINSDIR\cilogg_updater.exe"
        ${EndIf}
    ${EndIf}
    ${CiloggLog} "Setup cancelled before replacing files: the installation folder could not be prepared (result $0)"
    SetErrorLevel 5
    Abort "The CILogg folder could not be prepared. Installation cancelled before replacing files."
FunctionEnd

; Puts the moved-aside previous files back (once).
Function CiloggRollback
    ${If} $CiloggPrepared == 1
        ${CiloggLog} "Restoring the previous files"
        nsExec::ExecToLog '"$PLUGINSDIR\cilogg_updater.exe" --installer-rollback "$CiloggDir" --log "$CiloggLog"'
        Pop $0
        StrCpy $CiloggPrepared 0
        ${If} $0 != 0
            StrCpy $CiloggRestoreFailed 1
            ${CiloggLog} "Some previous files could not be restored (result $0); run setup again"
        ${EndIf}
    ${EndIf}
FunctionEnd

; Also after NSIS itself stopped the installation (a file it could not write, in GUI mode once
; the user closes the page).
Function .onInstFailed
    ${If} $CiloggPrepared == 1
        Call CiloggRollback
        ${If} $CiloggRestoreFailed == 1
            SetErrorLevel 4
        ${Else}
            SetErrorLevel 3
        ${EndIf}
    ${EndIf}
    ${CiloggLog} "CILogg ${VERSION} setup failed"
FunctionEnd

# Installer sections
Section "CILogg" cilogg
    ; Prevent this section from being unselected
    SectionIn RO

    Call CiloggPrepareInstallDirectory
    SetOutPath $INSTDIR
    ClearErrors
    ; Every executable is written by the last section, once the files it loads are in place.
    File /nonfatal release\tbb12.dll

    File COPYING
    File NOTICE
    File README.md
    File DOCUMENTATION.md
    File MCP.md
    File release\documentation.html
    SetOutPath $INSTDIR\python_runtime
    File /nonfatal /r /x *.exe release\python_runtime\*
    SetOutPath $INSTDIR\plugins
    File /r /x *.exe release\plugins\*
    SetOutPath $INSTDIR\plugin-sdk
    File /r /x *.exe release\plugin-sdk\*
    SetOutPath $INSTDIR
    File release\plugin-sdk.md
    ${If} ${Errors}
        StrCpy $CiloggCopyFailed 1
    ${EndIf}

!ifndef CILOGG_TEST_INSTALL
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
!endif

    ; Create uninstaller
    ClearErrors
    WriteUninstaller "$INSTDIR\Uninstall.exe"
    ${If} ${Errors}
        StrCpy $CiloggCopyFailed 1
    ${EndIf}
SectionEnd

Section "Qt Runtime libraries" qtlibs
    SetOutPath $INSTDIR
    ClearErrors
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
    ${If} ${Errors}
        StrCpy $CiloggCopyFailed 1
    ${EndIf}

SectionEnd

Section "MSVC Runtime libraries" vcruntime
    SetOutPath $INSTDIR
    ClearErrors
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
    ${If} ${Errors}
        StrCpy $CiloggCopyFailed 1
    ${EndIf}

SectionEnd

Section "Create Start menu shortcut" shortcut
!ifndef CILOGG_TEST_INSTALL
    SetShellVarContext all
    CreateShortCut "$SMPROGRAMS\cilogg.lnk" "$INSTDIR\cilogg.exe" "" "$INSTDIR\cilogg.exe" 0
!endif
SectionEnd

Section /o "Associate with .log files" associate
!ifndef CILOGG_TEST_INSTALL
    ${registerExtension} "$INSTDIR\cilogg.exe" ".log" "Log file"
!endif
SectionEnd

; The executable is written last: a CILogg started from the folder while files are copied
; fails to start instead of loading a mix of old and new files. Then the moved-aside files are
; deleted (or, while still in use, left for the next update and the next restart) and the ones
; this installation did not replace are put back.
Section "-CILogg executable" ciloggexe
    ${If} $CiloggCopyFailed != 1
        ; Only into a complete folder: a CILogg MCP server that keeps running starts
        ; cilogg_grep.exe and Python from here at any time.
        ClearErrors
        SetOutPath $INSTDIR\python_runtime
        File /nonfatal /r release\python_runtime\*.exe
        SetOutPath $INSTDIR\plugins
        File /nonfatal /r release\plugins\*.exe
        SetOutPath $INSTDIR\plugin-sdk
        File /nonfatal /r release\plugin-sdk\*.exe
        SetOutPath $INSTDIR
        File release\cilogg_grep.exe
        File release\cilogg_updater.exe
        File /nonfatal release\cilogg_crashpad_handler.exe
        File /nonfatal release\cilogg_minidump_dump.exe
        File release\cilogg.exe
        ${If} ${Errors}
            StrCpy $CiloggCopyFailed 1
        ${EndIf}
    ${EndIf}
    ${If} $CiloggCopyFailed == 1
        ${CiloggLog} "Copying the new files into $INSTDIR failed"
        Call CiloggRollback
        ${If} $CiloggRestoreFailed == 1
            SetErrorLevel 4
            Abort "CILogg files could not be written, and the previous version could not be fully restored. Run setup again."
        ${EndIf}
        SetErrorLevel 3
        Abort "CILogg files could not be written. The previous version was restored."
    ${EndIf}
    nsExec::ExecToLog '"$PLUGINSDIR\cilogg_updater.exe" --installer-finish "$CiloggDir" --log "$CiloggLog"'
    Pop $0
    StrCpy $CiloggPrepared 0
    ${If} $0 != 0
        ${CiloggLog} "Some files of the previous version that this installation does not replace could not be put back (result $0); run setup again"
        SetErrorLevel 6
        MessageBox MB_OK|MB_ICONEXCLAMATION "CILogg was installed, but some files of the previous version could not be put back. Run setup again." /SD IDOK
    ${EndIf}
    ${CiloggLog} "CILogg ${VERSION} installed in $INSTDIR"
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
    RMDir /r /REBOOTOK "$INSTDIR\plugins"
    RMDir /r /REBOOTOK "$INSTDIR\plugin-sdk"
    Delete "$INSTDIR\plugin-sdk.md"
    Delete "$INSTDIR\Uninstall.exe"

    Delete "$INSTDIR\cilogg.exe"
    Delete "$INSTDIR\cilogg_grep.exe"
    Delete "$INSTDIR\cilogg_updater.exe"
    Delete "$INSTDIR\cilogg_crashpad_handler.exe"
    Delete "$INSTDIR\cilogg_minidump_dump.exe"
    ; Files an update moved aside while a process used them, and its record.
    Delete /REBOOTOK "$INSTDIR\*.cilogg-old*"
    Delete "$INSTDIR\cilogg-update-moves.txt"
    Delete "$INSTDIR\cilogg-installer.log"
    Delete "$INSTDIR\cilogg-installer-report.txt"
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
    RMDir /r /REBOOTOK "$INSTDIR\python_runtime"
    RMDir /r /REBOOTOK "$INSTDIR\platforms"
    RMDir /r /REBOOTOK "$INSTDIR\generic"
    RMDir /r /REBOOTOK "$INSTDIR\iconengines"
    RMDir /r /REBOOTOK "$INSTDIR\imageformats"
    RMDir /r /REBOOTOK "$INSTDIR\networkinformation"
    RMDir /r /REBOOTOK "$INSTDIR\sqldrivers"
    RMDir /r /REBOOTOK "$INSTDIR\styles"
    RMDir /r /REBOOTOK "$INSTDIR\tls"
    RMDir /r /REBOOTOK "$INSTDIR\translations"
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
