
Unicode true
SetCompressor /SOLID lzma
SetCompressorDictSize 8

!include "MUI2.nsh"
!include "LogicLib.nsh"

!define APP_NAME       "VLCFlix Studio"
!define APP_SLUG       "VLCFlixStudio"
!define APP_EXE        "VLCFlixStudio.exe"
!define APP_PUBLISHER  "VLCFlix"
!define APP_URL        "https://github.com/JohnOlowe/VLCFlix"
!define UNINST_KEY     "Software\Microsoft\Windows\CurrentVersion\Uninstall\VLCFlixStudio"
!define APP_ID         "VLCFlixStudio.Native.2"
!define PROTECTED_EXE     "VLCFlix.exe"
!define PROTECTED_DIR     "$LOCALAPPDATA\Programs\VLCFlix"

!ifndef VERSION
  !define VERSION "0.0.0-dev"
!endif
!ifndef STAGE
  !error "STAGE must point at the built payload directory"
!endif
!ifndef OUTDIR
  !define OUTDIR "dist"
!endif
!ifndef ICONPATH
  !define ICONPATH ""
!endif

Name "${APP_NAME} ${VERSION}"
OutFile "${OUTDIR}\VLCFlixStudio-Setup-${VERSION}.exe"
InstallDir "$LOCALAPPDATA\Programs\${APP_SLUG}"
InstallDirRegKey HKCU "Software\${APP_SLUG}" "InstallDir"
RequestExecutionLevel user
BrandingText "${APP_NAME} ${VERSION}"
ShowInstDetails show
ShowUninstDetails show

VIProductVersion "${VERSION}.0"
VIAddVersionKey "ProductName"     "${APP_NAME}"
VIAddVersionKey "FileDescription" "${APP_NAME} installer"
VIAddVersionKey "FileVersion"     "${VERSION}"
VIAddVersionKey "ProductVersion"  "${VERSION}"
VIAddVersionKey "LegalCopyright"  "MIT licensed"

!define MUI_ABORTWARNING
!define MUI_FINISHPAGE_RUN "$INSTDIR\${APP_EXE}"
!define MUI_FINISHPAGE_RUN_TEXT "Run ${APP_NAME}"
!if "${ICONPATH}" != ""
  !define MUI_ICON "${ICONPATH}"
  !define MUI_UNICON "${ICONPATH}"
!endif

!insertmacro MUI_PAGE_WELCOME
!insertmacro MUI_PAGE_DIRECTORY
!insertmacro MUI_PAGE_INSTFILES
!insertmacro MUI_PAGE_FINISH
!insertmacro MUI_UNPAGE_CONFIRM
!insertmacro MUI_UNPAGE_INSTFILES
!insertmacro MUI_LANGUAGE "English"

; ------------------------------------------------------------------ install
Function .onInit
  ${If} ${FileExists} "${PROTECTED_DIR}\${PROTECTED_EXE}"
  ${AndIf} $INSTDIR == "${PROTECTED_DIR}"
    MessageBox MB_ICONSTOP|MB_OK \
      "This folder is protected. Choose a different installation folder."
    Abort
  ${EndIf}

  ; Refuse to write into any directory that already contains the old executable,
  ; whoever put it there.
  ${If} ${FileExists} "$INSTDIR\${PROTECTED_EXE}"
    MessageBox MB_ICONSTOP|MB_OK \
      "This folder contains protected files. Choose a different installation folder."
    Abort
  ${EndIf}
FunctionEnd

Section "Install" SecInstall
  SetOutPath "$INSTDIR"
  File /r "${STAGE}\*.*"

  ; Start Menu and desktop shortcuts, named for the new product only.
  CreateDirectory "$SMPROGRAMS\${APP_NAME}"
  CreateShortcut "$SMPROGRAMS\${APP_NAME}\${APP_NAME}.lnk" "$INSTDIR\${APP_EXE}"
  CreateShortcut "$DESKTOP\${APP_NAME}.lnk" "$INSTDIR\${APP_EXE}"

  WriteRegStr HKCU "Software\${APP_SLUG}" "InstallDir" "$INSTDIR"
  WriteRegStr HKCU "Software\${APP_SLUG}" "Version" "${VERSION}"

  ; Uninstall entry under the new product's key only.
  WriteRegStr   HKCU "${UNINST_KEY}" "DisplayName"     "${APP_NAME}"
  WriteRegStr   HKCU "${UNINST_KEY}" "DisplayVersion"  "${VERSION}"
  WriteRegStr   HKCU "${UNINST_KEY}" "Publisher"       "${APP_PUBLISHER}"
  WriteRegStr   HKCU "${UNINST_KEY}" "URLInfoAbout"    "${APP_URL}"
  WriteRegStr   HKCU "${UNINST_KEY}" "DisplayIcon"     "$INSTDIR\${APP_EXE}"
  WriteRegStr   HKCU "${UNINST_KEY}" "UninstallString" '"$INSTDIR\uninstall.exe"'
  WriteRegStr   HKCU "${UNINST_KEY}" "QuietUninstallString" '"$INSTDIR\uninstall.exe" /S'
  WriteRegDWORD HKCU "${UNINST_KEY}" "NoModify" 1
  WriteRegDWORD HKCU "${UNINST_KEY}" "NoRepair" 1

  WriteUninstaller "$INSTDIR\uninstall.exe"
SectionEnd

; ---------------------------------------------------------------- uninstall
Section "Uninstall"
  Delete "$SMPROGRAMS\${APP_NAME}\${APP_NAME}.lnk"
  RMDir  "$SMPROGRAMS\${APP_NAME}"
  Delete "$DESKTOP\${APP_NAME}.lnk"

  ; The payload, named file by file: a wildcard delete in the install directory
  ; is how one application deletes another's files when the two share a folder.
  Delete "$INSTDIR\${APP_EXE}"
  Delete "$INSTDIR\LICENSE.txt"
  Delete "$INSTDIR\README.txt"
  Delete "$INSTDIR\libvlc.dll"
  Delete "$INSTDIR\libvlccore.dll"
  Delete "$INSTDIR\*.dll"
  RMDir /r "$INSTDIR\plugins"
  Delete "$INSTDIR\uninstall.exe"
  RMDir "$INSTDIR"

  DeleteRegKey HKCU "${UNINST_KEY}"
  DeleteRegKey HKCU "Software\${APP_SLUG}"

  ; The user's library, settings and logs stay: %LOCALAPPDATA%\VLCFlixStudio.
  ; Uninstalling a player must not delete films from someone's disk. The app
  ; offers to remove its own data from its settings screen instead.
  MessageBox MB_ICONINFORMATION|MB_OK \
    "Your library, settings and logs are kept in $LOCALAPPDATA\${APP_SLUG}."
SectionEnd
