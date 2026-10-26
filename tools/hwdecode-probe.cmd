@echo off
rem ===================================================================
rem  VLCFlix - hardware decode probe
rem
rem  Double-click it, or drag a film onto it. It writes a report that
rem  says which GPU decode plans this machine can actually run, using
rem  the same ffmpeg command lines VLCFlix 1.2.0 tries.
rem
rem  Notes:
rem   - it never touches your films; each trial writes a small temp file
rem   - it needs ffmpeg on PATH (the same ffmpeg VLCFlix uses)
rem   - nothing is installed and no settings are changed
rem ===================================================================
setlocal EnableExtensions EnableDelayedExpansion
title VLCFlix hardware-decode probe

set "HERE=%~dp0"
set "SRC=%~1"
set "REPORT=%TEMP%\vlcflix-hwdecode-report.txt"
set "OUT=%TEMP%\vlcflix-probe-out.mp4"
set "ERRLOG=%TEMP%\vlcflix-probe-err.txt"

where ffmpeg >nul 2>&1
if errorlevel 1 (
  echo.
  echo ffmpeg was not found on PATH.
  echo VLCFlix needs the same ffmpeg on PATH. Install it, open a new
  echo window, and run this again.
  echo.
  pause
  exit /b 1
)
for /f "delims=" %%P in ('where ffmpeg') do set "FFMPEG=%%P"
where ffprobe >nul 2>&1 && for /f "delims=" %%P in ('where ffprobe') do set "FFPROBE=%%P"

> "%REPORT%" echo VLCFlix hardware-decode probe
>>"%REPORT%" echo Date      : %DATE% %TIME%
>>"%REPORT%" echo Computer  : %COMPUTERNAME%
>>"%REPORT%" echo ffmpeg    : %FFMPEG%
>>"%REPORT%" echo.
>>"%REPORT%" echo --- ffmpeg version ---
"%FFMPEG%" -hide_banner -version 2>&1 | findstr /b /c:"ffmpeg version" >>"%REPORT%"
>>"%REPORT%" echo.
>>"%REPORT%" echo --- hardware acceleration methods this build lists ---
"%FFMPEG%" -hide_banner -hwaccels 2>&1 >>"%REPORT%"
>>"%REPORT%" echo.
>>"%REPORT%" echo --- hardware encoders present (informational) ---
"%FFMPEG%" -hide_banner -encoders 2>&1 | findstr /i "qsv nvenc amf vaapi" >>"%REPORT%"
>>"%REPORT%" echo.

rem --- the film to test: dragged in, or made here ---------------------
if "%SRC%"=="" (
  set "SRC=%TEMP%\vlcflix-probe-source.mp4"
  echo No film given - making a 3 s 10-bit HEVC test clip...
  "%FFMPEG%" -hide_banner -loglevel error -y -f lavfi -i testsrc2=size=1280x720:rate=24 -t 3 -c:v libx265 -pix_fmt yuv420p10le -an "!SRC!" 2>nul
  if not exist "!SRC!" (
    echo This ffmpeg has no libx265, so a test clip cannot be made.
    echo Drag a film onto this file instead.
    echo.
    pause
    exit /b 1
  )
)
>>"%REPORT%" echo --- source ---
>>"%REPORT%" echo file      : %SRC%
if defined FFPROBE (
  for /f "delims=" %%L in ('"%FFPROBE%" -v error -select_streams v:0 -show_entries stream=codec_name,pix_fmt,width,height -of default=nw=1 "%SRC%" 2^>nul') do >>"%REPORT%" echo %%L
)
>>"%REPORT%" echo.

>>"%REPORT%" echo --- trials (each writes at most 60 frames) ---
set "RESULT="

call rem ---------------------------------------------------------------
:plan
set "NAME=%~1"
set "INFLAGS=%~2"
set "OUTFLAGS=%~3"
set "VERDICT=FAIL"
del "%OUT%" >nul 2>&1
type nul > "%ERRLOG%"
"%FFMPEG%" -hide_banner -nostdin -y -v error -t 3 %INFLAGS% -i "%SRC%" -an -sn -dn %OUTFLAGS% -c:v libx264 -preset veryfast -crf 28 -pix_fmt yuv420p -frames:v 60 "%OUT%" 2>>"%ERRLOG%"
set "RC=%ERRORLEVEL%"
set "SIZE=0"
if exist "%OUT%" for %%F in ("%OUT%") do set "SIZE=%%~zF"
set "FRAMES=0"
if defined FFPROBE if not "%SIZE%"=="0" for /f "delims=" %%N in ('"%FFPROBE%" -v error -count_frames -select_streams v:0 -show_entries stream=nb_read_frames -of csv=p=0 "%OUT%" 2^>nul') do set "FRAMES=%%N"
if "%RC%"=="0" if not "%SIZE%"=="0" if not "%FRAMES%"=="0" set "VERDICT=OK"
echo [!VERDICT!] %NAME%
>>"%REPORT%" echo [!VERDICT!] %NAME%
>>"%REPORT%" echo         command: -t 3 %INFLAGS% -i FILM %OUTFLAGS% -c:v libx264 ...
>>"%REPORT%" echo         exit %RC%, %SIZE% bytes, %FRAMES% frames
if "%VERDICT%"=="FAIL" >>"%REPORT%" echo         ffmpeg said:
if "%VERDICT%"=="FAIL" >>"%REPORT%" type "%ERRLOG%"
set "RESULT=%RESULT% %NAME% = %VERDICT%;"
exit /b 0
