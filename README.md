# F18-RWR

An RWR (radar warning receiver) repeater for the F/A-18C in DCS World. A small round display on your cockpit
panel shows the same picture as the Hornet's own RWR.

DCS gives scripts no RWR contacts for the Hornet. Instead its RWR display is drawn into a hidden corner of the DCS
window, captured with ffmpeg, and streamed to the board as video. While no video arrives, the board shows a status
screen.

### What it does

- `dcs/Install-RwrViewport.ps1` adds two lines that tell the Hornet to also draw its RWR into a viewport named
  `F18_RWR`. This is the same way the AMPCD gets exported.
- `receiver/rwr_stream.bat` uses ffmpeg to capture that area of the DCS window and sends it to the board as JPEG video.
- `F18_RWR.ino` shows the frames and adds the rings and tick marks from the real instrument's face. It also makes the
  picture brighter, filters out JPEG noise and colours it green. The buzzer is held off.
- Nothing reads `Export.lua` or calls `LoGet*`. DCS does not give the Hornet's RWR contacts to scripts anyway.
- The board can only show the pixels DCS already drew on the cockpit RWR. That means no emitter IDs, no exact
  bearings and no signal strength values.
- The firmware does not analyse the image. It does not track symbols, raise its own alerts or keep a history.

The only benefit is not having to look down into the cockpit, which is the same as any physical home-cockpit RWR.

### The catch: multiplayer integrity checks

This is not a monitor-setup change only. It also edits a file inside the game folder:
`Mods\aircraft\FA-18C\Cockpit\Scripts\TEWS\indicator\BAKE\RWR_ALR67_bake_init.lua`.

- On multiplayer servers that enforce the integrity check, a changed cockpit script can fail it. You may get kicked,
  or the server may not let you into the Hornet.
- This is about file integrity, not cheating. No server admin would see an advantage in this setup, but the check
  cannot tell a harmless change from a harmful one, so it will still flag it.
- If a server you play on rejects the file, restore the `.orig` copy before joining (see [Undoing it](#undoing-it)),
  or run a DCS repair.

## Parts

- **Waveshare ESP32-S3-LCD-2.8C**: 480×480 round ST7701 panel, 16 MB flash, OPI PSRAM, buzzer.
- A USB-C data cable (not a charge-only one) to flash the board.
- A Windows 10 (version 1903 or later) or Windows 11 PC running DCS, on the same Wi-Fi network as the board.
  Wi-Fi must be 2.4 GHz (the ESP32 cannot join 5 GHz), and the router must not isolate clients from each other.
- **ffmpeg 8 or later** on the PATH (the `gfxcapture` filter is needed), driven by `receiver\rwr_stream.bat`.
- To build: **Python 3.11**, about 8 GB of free disk space (the toolchain and ESP-IDF in `.piocore` take 7.3 GB)
  and an internet connection for the first build. `pio.ps1` installs PlatformIO itself.

## What is in the folder

| Path | What it is |
|---|---|
| `F18_RWR/` | The board firmware source. See [F18_RWR/README.md](F18_RWR/README.md) for its options. |
| `platformio.ini` | Build settings: board, libraries, COM port and the custom sdkconfig. |
| `pio.ps1` | Runs the project's own PlatformIO (installs it into `.piocore` on first use). |
| `dcs/Install-RwrViewport.ps1` | Makes the Hornet draw its RWR into the capture area. |
| `receiver/rwr_stream.bat` | Streams the Hornet RWR to the board with ffmpeg. |
| `receiver/rwr_test.bat` | Streams a test pattern to the board, for checking it without DCS. |

## 0. Start from a clean Windows install

Do this once, in order. Commands are for PowerShell; `winget` ships with current Windows 10/11.

1. **Install the tools:**
   ```
   winget install --id Git.Git -e
   winget install --id Python.Python.3.11 -e
   winget install --id Gyan.FFmpeg -e
   ```
   Open a **new** PowerShell window afterwards so the new programs are on the PATH.
   - **Python 3.11** runs PlatformIO. `pio.ps1` finds it through the `py` launcher that this installer adds.
     Typing `python` alone may open the Microsoft Store; that stub is not used.
   - **ffmpeg** must be version 8 or later. Check it with `ffmpeg -filters | findstr gfxcapture`: it must print a
     `gfxcapture` line. If it prints nothing, the ffmpeg is too old (check `ffmpeg -version`).
2. **USB driver.** The board's USB-serial chip is a WCH CH343. Windows 11 usually installs it by itself. If the board
   shows in Device Manager under *Other devices* or with a warning icon, install the driver from
   [wch-ic.com](https://www.wch-ic.com/downloads/CH343SER_EXE.html) and replug the board.
3. **Get the project.** Clone it, or download the ZIP from the repository and unzip it:
   ```
   git clone <repository-url> C:\Projects\F18-RWR
   cd C:\Projects\F18-RWR
   ```
   Keep the path short and without spaces, such as `C:\Projects\F18-RWR`. The ESP-IDF build creates deep folder
   paths and can fail under long ones.
4. **PowerShell scripts.** PowerShell blocks scripts by default. Allow local scripts once for your user:
   ```
   Set-ExecutionPolicy -Scope CurrentUser RemoteSigned
   ```
   Or run each command as `powershell -ExecutionPolicy Bypass -File .\pio.ps1 ...`.
5. **Firewall.** The first time ffmpeg runs, Windows asks to allow it on the private network. Allow it.
6. **DCS World** must already be installed. Note its folder (the default for the install script is `G:\DCS World`;
   Steam installs live under `...\steamapps\common\DCSWorld`). Start DCS once so `Saved Games\DCS` exists.

Then continue with sections 1 to 3.

## 1. Build and flash the board

1. **Wi-Fi.** Copy `F18_RWR\wifi_config.example.h` to `F18_RWR\wifi_config.h` and put in your network name and
   password. The file is git-ignored. (The SARI's `wifi_config.h` has the same format and can be copied as it is.)
2. **COM port.** Plug in the board and find its port in Device Manager under *Ports* (a CH343
   "USB-Enhanced-SERIAL" device), or with `.\pio.ps1 device list` once step 3 has run. Set `upload_port` and
   `monitor_port` in `platformio.ini` to it. They are set to the placeholder `COM3` in the repository.
3. **Build and flash:**
   ```
   .\pio.ps1 run -t upload
   ```
   The **first run is slow**: it creates `.piocore\venv`, installs PlatformIO, downloads the pioarduino platform,
   toolchain and ESP-IDF into `.piocore\core`, then rebuilds the Arduino libraries with the custom sdkconfig.
   Allow from 15 to 30 minutes or more. Later builds take under a minute.

   If the upload cannot connect, hold the board's **BOOT** button while plugging it in, then try again.
4. The board shows `JOINING WIFI`, then `NO VIDEO` with its IP address. **Reserve that address in your router**,
   because the batch files send to it.

Other commands:

| Command | Does |
|---|---|
| `.\pio.ps1 run` | Build only. |
| `.\pio.ps1 run -t upload` | Build and flash to the port in `platformio.ini`. |
| `.\pio.ps1 run -t upload --upload-port COM5` | Flash to another port this once. |
| `.\pio.ps1 device list` | List serial ports. |
| `.\pio.ps1 run -t clean` | Delete the build output, so the next build starts over. |

PlatformIO here is **project-local**: `pio.ps1` installs it into the project's `.piocore` folder, pinned to
PlatformIO Core 6.2.0 and pioarduino 55.03.312-1. It does not use or change a PlatformIO that VS Code may have
installed, and VS Code is not needed.

### Why PlatformIO

The Arduino IDE links the ESP-IDF libraries that come precompiled with the board package. Those run the display's
refresh interrupt from flash, so a flash or PSRAM stall starves it, and the picture jumps, doubles or shows lines.
PlatformIO (with the pioarduino platform) rebuilds those libraries with the settings in `platformio.ini` under
`custom_sdkconfig`:

| Setting | Effect |
|---|---|
| `CONFIG_LCD_RGB_ISR_IRAM_SAFE` | The panel refresh interrupt runs from internal RAM. |
| `CONFIG_GDMA_CTRL_FUNC_IN_IRAM`, `CONFIG_GDMA_ISR_IRAM_SAFE` | So does the DMA that feeds it. |
| `CONFIG_SPIRAM_XIP_FROM_PSRAM` | Program code runs from PSRAM, so flash access no longer stalls the cache. |
| `CONFIG_ESP32S3_DATA_CACHE_LINE_64B` | Larger cache lines for faster PSRAM reads. |

With these, the board runs the stream at 12 frames a second with no glitches.

## 2. Check it without DCS

1. Edit `BOARD` at the top of `receiver\rwr_test.bat` to the board's address.
2. Run `receiver\rwr_test.bat`. Green symbols should move around the board's screen. Press Ctrl+C to stop.

The board takes **one stream at a time**. If `receiver\rwr_stream.bat` is running, close it first, or the test
waits behind it.

Or send ffmpeg's test pattern directly:
```
ffmpeg -re -f lavfi -i testsrc2=size=480x480:rate=12 -t 15 -c:v mjpeg -q:v 5 -pix_fmt yuvj420p -huffman default -f mjpeg tcp://<board-ip>:7791
```
Red should be on the left and the checkerboard at the bottom right.

## 3. Stream the cockpit RWR

One-time setup:

1. **Install the viewport patch.** Run in PowerShell (add `-DcsDir` if DCS is not in `G:\DCS World`):
   ```
   powershell -ExecutionPolicy Bypass -File dcs\Install-RwrViewport.ps1
   ```
   It appends two lines to the Hornet's `Mods\aircraft\FA-18C\Cockpit\Scripts\TEWS\indicator\BAKE\RWR_ALR67_bake_init.lua`
   (the original is kept as `.orig`). **DCS updates and repairs restore the original file, so run it again after one.**
2. **Add the viewport to your monitor setup.** In the MonitorSetup file you use
   (`Saved Games\DCS\Config\MonitorSetup\`, selected in DCS under Options > System), add a 480×480 area that
   lies inside the DCS window but that no monitor shows, and name it `F18_RWR`:
   ```lua
   rwr = { x = 3712; y = 912; width = 480; height = 480; }
   F18_RWR = rwr
   ```
   The numbers above are for this PC: a 5488×1536 DCS window, with the area right of the main screen and
   below the left DDI. Choose coordinates that fit your own layout. Then **restart DCS fully**, because it
   reads the monitor setup only at startup.
3. ffmpeg 8 or later must be installed (section 0, step 1).
4. **DCS window size.** The capture crops the viewport out of the DCS window, so the window must be the full size of the
   monitor setup, including the hidden area. If DCS runs in a smaller window than the setup (for example a 3440×1440
   screen when the setup is 5488×1536), the viewport lies outside the window and ffmpeg fails with
   `Picture size ... is invalid`.
5. **Set up the batch file.** Edit the settings at the top of `receiver\rwr_stream.bat`: `BOARD` is the board's
   address from section 1, and `RWR_X`, `RWR_Y`, `WIN_W` and `WIN_H` must match your viewport and DCS window.

Every session: run `receiver\rwr_stream.bat` and leave it open. It waits for DCS and the board, and reconnects
when either restarts. The board takes **one stream at a time**, so stop the test batch file before starting it.

Get into the Hornet with the RWR on. Radar emitters then appear on the board. The board's buzzer stays off.

## What the board shows

| Screen | Meaning |
|---|---|
| `JOINING WIFI` | Connecting to the network. |
| `NO VIDEO` + IP address | No video frame for 1.5 s. This is the address the stream goes to. |
| Video of the cockpit RWR with rings and ticks | The stream is live. |

## Network ports

| Port | Used for |
|---|---|
| TCP 7791 (board) | JPEG video stream from ffmpeg. |

The Windows firewall must allow ffmpeg to connect out, and the PC and board must be on the same network.

## Serial log

Open the board's port at 115200 baud. This board's USB chip can show nothing in some serial monitors; reading the
port from PowerShell works:
```
$p = New-Object System.IO.Ports.SerialPort COM3,115200; $p.Open(); Start-Sleep 10; $p.ReadExisting(); $p.Close()
```
The log shows the Wi-Fi join, each video connection, every 100th frame, and every 5 s while the picture changes
the number of frames drawn, the draw time and the free heap.

## Changing the firmware

The source is in `F18_RWR\`; `platformio.ini` points PlatformIO there with `src_dir = F18_RWR`. The constants at the
top of `F18_RWR.ino` are described in [F18_RWR/README.md](F18_RWR/README.md). Build and flash again with
`.\pio.ps1 run -t upload`.

Changing anything under `custom_sdkconfig` makes the next build rebuild the Arduino libraries again (slow, once).

## Troubleshooting

| Symptom | Check |
|---|---|
| Batch file keeps printing "stopped ... retrying" | Either ffmpeg cannot find the DCS window or cannot reach the board. DCS must be running, ffmpeg must be version 8 or later with `gfxcapture`, and `BOARD` must be the address the board shows. |
| Stream runs but the board stays black | DCS is not drawing into the viewport. Check the patch is applied (rerun the script after a DCS update), the `F18_RWR` entry is in the monitor setup you actually use, DCS was restarted, and you are in the cockpit with the RWR on. |
| Capture is cropped wrong | The viewport must lie inside the DCS window, and `WIN_W`/`WIN_H` must match the real window size. |
| `Picture size ... is invalid` from ffmpeg | The DCS window is smaller than the monitor setup, so the viewport is outside it. See section 3, step 4. |
| `Python 3.11 not found` from `pio.ps1` | Install it with `winget install --id Python.Python.3.11 -e` and open a new PowerShell window. |
| `pio.ps1 cannot be loaded because running scripts is disabled` | Set the execution policy (section 0, step 4), or use `powershell -ExecutionPolicy Bypass -File .\pio.ps1 ...`. |
| First build fails while downloading | Run it again; it continues where it stopped. If it keeps failing, delete `.piocore` and start over. |
| Build fails with a missing `.ino.cpp` or similar file | Run the build again. If it persists, run `.\pio.ps1 run -t clean` first. |
| Build fails with path or file-name errors | Move the project to a short path without spaces. |
| `Could not open COM3` | Wrong port, or another program (a serial monitor) has it open. |
| Upload fails or the COM port is missing | Use a data cable, install the CH343 driver, and hold BOOT while plugging in. |


To start the build completely fresh, delete `.pio` (build output) and `.piocore` (PlatformIO and its toolchains);
the next `.\pio.ps1 run` installs everything again.

## Undoing it

- `RWR_ALR67_bake_init.lua.orig` beside the patched Hornet file is its original. A DCS repair also restores it.
- `Saved Games\DCS\Config\MonitorSetup\<name>.lua.bak-before-rwr` is the monitor setup from before the viewport was added.
- To use the board for something else, flash that project's firmware to its port.

## Known limitations

- DCS gives export scripts no RWR contacts for the Hornet, so the video route is the only way to get its RWR.
  ([discussion](https://forum.dcs.world/topic/294007-logettwsinfo-returns-nill-while-sensor-export-is-true/))
- The exported picture has the symbols only. The rings and ticks are drawn by the board, sized from DCS's own ring radii
  (set `DRAW_RINGS` to `false` in `F18_RWR.ino` if your display has physical rings over it).
- Some multiplayer servers may reject modified cockpit files.
- The Hornet file patch has to be redone after every DCS update or repair.

## License

This project is licensed under a [Custom Non-Commercial License](LICENSE). You can use and modify it for
**personal and educational purposes only**. For commercial use, please contact me for permission.
