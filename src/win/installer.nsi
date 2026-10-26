; ---------------------------------------------------------------------------
; VLCFlix installer
;
; Per-user install into %LOCALAPPDATA%\Programs\VLCFlix so that no elevation
; prompt is needed, plus a desktop icon and Start Menu entry. Everything is
; removed on uninstall except the user's data, which is only deleted when they
; explicitly ask for it.
;
; Built with makensis on Linux as part of scripts/build-win.sh:
;   -DVERSION=1.0.0 -DSTAGE=<payload dir> -DOUTDIR=<dist dir>
;   -DICONPATH=<absolute path to icon.ico> -DLICENSEPATH=<absolute path to LICENSE.txt>
;
; Absolute paths are passed in rather than using ${__FILEDIR__}, which resolves
; incorrectly when the script itself is given as a relative path.
; ---------------------------------------------------------------------------

Unicode true
SetCompressor /SOLID lzma
; 32 MB was enough to run the compressor out of memory on a small builder.
SetCompressorDictSize 8

!include "MUI2.nsh"
!include "FileFunc.nsh"
!include "LogicLib.nsh"

!define APP_NAME     "VLCFlix"
!define APP_EXE      "VLCFlix.exe"
!define APP_PUBLISHER "VLCFlix"
!define APP_URL      "https://github.com/JohnOlowe/VLCFlix"
!define UNINST_KEY   "Software\Microsoft\Windows\CurrentVersion\Uninstall\VLCFlix"

Name "${APP_NAME} ${VERSION}"
OutFile "${OUTDIR}\VLCFlix-Setup-${VERSION}.exe"
InstallDir "$LOCALAPPDATA\Programs\${APP_NAME}"
InstallDirRegKey HKCU "Software\VLCFlix" "InstallDir"
RequestExecutionLevel user          ; never UAC: it is a per-user app
BrandingText "${APP_NAME} ${VERSION}"
ShowInstDetails show
ShowUninstDetails show

VIProductVersion "${VERSION}.0"
VIAddVersionKey "ProductName"     "${APP_NAME}"
VIAddVersionKey "CompanyName"     "${APP_PUBLISHER}"
VIAddVersionKey "FileDescription" "${APP_NAME} installer"
VIAddVersionKey "FileVersion"     "${VERSION}.0"
VIAddVersionKey "LegalCopyright"  "Free software"

!define MUI_ICON   "${ICONPATH}"
!define MUI_UNICON "${ICONPATH}"
!define MUI_ABORTWARNING
!define MUI_FINISHPAGE_RUN "$INSTDIR\${APP_EXE}"
!define MUI_FINISHPAGE_RUN_TEXT "Start VLCFlix now"

!insertmacro MUI_PAGE_WELCOME
!insertmacro MUI_PAGE_LICENSE "${LICENSEPATH}"
!insertmacro MUI_PAGE_DIRECTORY
!insertmacro MUI_PAGE_INSTFILES
!insertmacro MUI_PAGE_FINISH

!insertmacro MUI_UNPAGE_CONFIRM
!insertmacro MUI_UNPAGE_INSTFILES

!insertmacro MUI_LANGUAGE "English"

; ---------------------------------------------------------------------------

Function .onInit
  ; Refuse to install twice side by side: it is the same app either way.
  ReadRegStr $0 HKCU "${UNINST_KEY}" "UninstallString"
  ${If} $0 != ""
    ${AndIfNot} ${Silent}
      MessageBox MB_OKCANCEL|MB_ICONQUESTION \
        "${APP_NAME} is already installed. This will replace the existing copy and keep your library settings." \
        IDOK continue
      Abort
    continue:
  ${EndIf}
FunctionEnd

Section "VLCFlix" SecMain
  SectionIn RO
  SetOutPath "$INSTDIR"

  ; Close a running copy first so files are not locked.
  nsExec::Exec 'taskkill /IM "${APP_EXE}" /F'
  Sleep 400

  File /r "${STAGE}\*.*"

  WriteRegStr HKCU "Software\VLCFlix" "InstallDir" "$INSTDIR"
  WriteRegStr HKCU "Software\VLCFlix" "Version" "${VERSION}"

  ; Programs and Features entry
  WriteRegStr   HKCU "${UNINST_KEY}" "DisplayName"     "${APP_NAME}"
  WriteRegStr   HKCU "${UNINST_KEY}" "DisplayVersion"  "${VERSION}"
  WriteRegStr   HKCU "${UNINST_KEY}" "DisplayIcon"     "$INSTDIR\${APP_EXE}"
  WriteRegStr   HKCU "${UNINST_KEY}" "Publisher"       "${APP_PUBLISHER}"
  WriteRegStr   HKCU "${UNINST_KEY}" "URLInfoAbout"    "${APP_URL}"
  WriteRegStr   HKCU "${UNINST_KEY}" "UninstallString" '"$INSTDIR\Uninstall.exe"'
  WriteRegStr   HKCU "${UNINST_KEY}" "QuietUninstallString" '"$INSTDIR\Uninstall.exe" /S'
  WriteRegDWORD HKCU "${UNINST_KEY}" "NoModify" 1
  WriteRegDWORD HKCU "${UNINST_KEY}" "NoRepair" 1
  ${GetSize} "$INSTDIR" "/S=0K" $0 $1 $2
  IntFmt $0 "0x%08X" $0
  WriteRegDWORD HKCU "${UNINST_KEY}" "EstimatedSize" "$0"

  WriteUninstaller "$INSTDIR\Uninstall.exe"
SectionEnd

Section "Shortcuts" SecShortcuts
  ; Desktop icon -- what was asked for.
  CreateShortcut "$DESKTOP\${APP_NAME}.lnk" "$INSTDIR\${APP_EXE}" "" \
                 "$INSTDIR\${APP_EXE}" 0 SW_SHOWNORMAL "" "Browse and play your library in VLC"

  ; Start Menu
  CreateDirectory "$SMPROGRAMS\${APP_NAME}"
  CreateShortcut "$SMPROGRAMS\${APP_NAME}\${APP_NAME}.lnk" "$INSTDIR\${APP_EXE}" "" \
                 "$INSTDIR\${APP_EXE}" 0
  CreateShortcut "$SMPROGRAMS\${APP_NAME}\Uninstall ${APP_NAME}.lnk" "$INSTDIR\Uninstall.exe"
SectionEnd

Section "Uninstall"
  nsExec::Exec 'taskkill /IM "${APP_EXE}" /F'
  Sleep 400

  Delete "$DESKTOP\${APP_NAME}.lnk"
  Delete "$SMPROGRAMS\${APP_NAME}\${APP_NAME}.lnk"
  Delete "$SMPROGRAMS\${APP_NAME}\Uninstall ${APP_NAME}.lnk"
  RMDir  "$SMPROGRAMS\${APP_NAME}"

  RMDir /r "$INSTDIR\resources"
  Delete "$INSTDIR\${APP_EXE}"
  Delete "$INSTDIR\Uninstall.exe"
  RMDir "$INSTDIR"

  DeleteRegKey HKCU "${UNINST_KEY}"
  DeleteRegKey HKCU "Software\VLCFlix"

  ; Ask before touching the library settings and generated artwork.
  ${IfNot} ${Silent}
    MessageBox MB_YESNO|MB_ICONQUESTION \
      "Also delete your VLCFlix library settings, watch progress and generated artwork?$\n$\nLocated in:%$\n$LOCALAPPDATA\VLCFlix" \
      IDNO keep
      RMDir /r "$LOCALAPPDATA\VLCFlix"
    keep:
  ${EndIf}
SectionEnd
