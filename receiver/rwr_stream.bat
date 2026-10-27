@echo off
rem Streams the F/A-18C RWR from the DCS window to the F18_RWR board as JPEG frames (TCP 7791).
rem
rem DCS draws the RWR into the F18_RWR viewport of the monitor setup (DDIs+MFCD-fixed.lua), an area
rem of the DCS window that no monitor shows. Windows Graphics Capture reads it from the window itself.
rem Leave this running: it waits for DCS and the board, and reconnects when either restarts.
rem Needs ffmpeg 8 or later (for gfxcapture) on the PATH.
rem The board's decoder (JPEGDEC) fails on ffmpeg's 4:4:4 JPEGs; keep yuvj420p and -huffman default.

rem The address the board shows on its screen (reserve it in your router)
set BOARD=192.168.1.100
set PORT=7791
set FPS=12
set QUALITY=2

rem Must match F18_RWR in the monitor setup, and DCS's resolution (the window size)
set RWR_X=3712
set RWR_Y=912
set RWR_SIZE=480
set WIN_W=5488
set WIN_H=1536

set /a CROP_R=WIN_W-RWR_X-RWR_SIZE
set /a CROP_B=WIN_H-RWR_Y-RWR_SIZE

:loop
echo [%time%] streaming DCS RWR to %BOARD%:%PORT%
ffmpeg -hide_banner -loglevel error ^
  -f lavfi -i "gfxcapture=window_exe=(?i).*dcs\.exe:max_framerate=%FPS%:capture_cursor=0:crop_left=%RWR_X%:crop_top=%RWR_Y%:crop_right=%CROP_R%:crop_bottom=%CROP_B%,hwdownload,format=bgra" ^
  -vf "scale=480:480" -c:v mjpeg -q:v %QUALITY% -pix_fmt yuvj420p -huffman default -flush_packets 1 ^
  -f mjpeg tcp://%BOARD%:%PORT%
echo [%time%] stopped (DCS not running or board not reachable), retrying in 3 s
rem ping as the delay: timeout fails when there is no console (run from another program)
ping -n 4 127.0.0.1 >nul
goto loop
