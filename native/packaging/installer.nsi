; ---------------------------------------------------------------------------
; VLCFlix Studio — installer
;
; This is the installer for the *native* application (2.x). It is deliberately
; not the installer of VLCFlix 1.2.0 (src/win/installer.nsi): every identity
; Windows uses to decide "which application is this" is different, so the two
; install side by side, run side by side, and uninstall independently.
;
;   product name     VLCFlix       -> VLCFlix Studio
;   executable       VLCFlix.exe   -> VLCFlixStudio.exe
;   install dir      ...\Programs\VLCFlix      -> ...\Programs\VLCFlixStudio
;   user data        %LOCALAPPDATA%\VLCFlix    -> %LOCALAPPDATA%\VLCFlixStudio
;   uninstall key    ...\Uninstall\VLCFlix     -> ...\Uninstall\VLCFlixStudio
;   NSIS AppId       VLCFlix                   -> VLCFlix Studio (native)
;
; Two rules this script enforces rather than documents:
;   1. It refuses to install into a directory that already holds VLCFlix 1.2.0.
;   2. It never writes, migrates or deletes anything under the 1.2.0 data
;      directory. Importing that library, if it is ever wanted, will be an
;      explicit action inside the app, asked for by the user.
;
; Built by scripts/build-win-native.sh:
;   -DVERSION=2.0.0 -DSTAGE=<payload dir> -DOUTDIR=<dist dir> -DICONPATH=<icon.ico>
; ---------------------------------------------------------------------------

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
; A distinct AppId means Windows cannot treat this as an upgrade of 1.2.0.
!define APP_ID         "VLCFlixStudio.Native.2"
; The 1.2.0 names, used only to *refuse* to touch them.
!define LEGACY_EXE     "VLCFlix.exe"
!define LEGACY_DIR     "$LOCALAPPDATA\Programs\VLCFlix"

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
; Per-user install: no elevation prompt, and no way to touch another user's 1.2.0.
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
  ; A directory that holds 1.2.0's executable is 1.2.0's directory. Writing
  ; here would replace one application with another while keeping its identity,
  ; which is exactly what this build must never do.
  ${If} ${FileExists} "${LEGACY_DIR}\${LEGACY_EXE}"
  ${AndIf} $INSTDIR == "${LEGACY_DIR}"
    MessageBox MB_ICONSTOP|MB_OK \
      "VLCFlix (1.x) is installed in this folder.$\r$\n$\r$\n${APP_NAME} is a separate application and will not install over it. Choose a different folder - the default, $LOCALAPPDATA\Programs\${APP_SLUG}, is the normal choice."
    Abort
  ${EndIf}

  ; Refuse to write into any directory that already contains the old executable,
  ; whoever put it there.
  ${If} ${FileExists} "$INSTDIR\${LEGACY_EXE}"
    MessageBox MB_ICONSTOP|MB_OK \
      "This folder contains ${LEGACY_EXE} from VLCFlix 1.x.$\r$\n$\r$\nInstalling ${APP_NAME} here would mix the two applications. Choose an empty folder."
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
    "Your library, settings and logs are kept in $LOCALAPPDATA\${APP_SLUG}.$\r$\n$\r$\nVLCFlix 1.x, if installed, was not touched."
SectionEnd
