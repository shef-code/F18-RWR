@echo off
rem Sends a fake RWR picture to the F18_RWR board: green symbols moving around on black, like the cockpit RWR.
rem Use it to check the display without DCS. The board takes one stream at a time, so stop rwr_stream.bat
rem first. Press Ctrl+C to stop.
rem Needs ffmpeg on the PATH.

rem The address the board shows on its screen (reserve it in your router)
set BOARD=192.168.1.100
set PORT=7791
set FPS=12
set QUALITY=2
rem Run from the fonts folder so the filter can name the font without a drive letter (its colon needs escaping)
pushd %WINDIR%\Fonts
set FONT=arialbd.ttf

set "T=fontfile=%FONT%:fontsize=44:fontcolor=0x30e060"
set "VF=drawtext=%T%:text=29:x=240+150*sin(t*0.5):y=240-150*cos(t*0.5)"
set "VF=%VF%,drawtext=%T%:text=11:x=240+95*sin(-t*0.8+2):y=240-95*cos(-t*0.8+2)"
set "VF=%VF%,drawtext=%T%:text=P:x=240+190*sin(t*0.3+4):y=240-190*cos(t*0.3+4)"
set "VF=%VF%,drawtext=%T%:text=S:x=240+55*sin(t*1.1+1):y=240-55*cos(t*1.1+1)"

echo Sending a test RWR picture to %BOARD%:%PORT% (Ctrl+C to stop)
ffmpeg -hide_banner -loglevel error -re -f lavfi -i "color=black:size=480x480:rate=%FPS%" -vf "%VF%" -c:v mjpeg -q:v %QUALITY% -pix_fmt yuvj420p -huffman default -flush_packets 1 -f mjpeg tcp://%BOARD%:%PORT%
