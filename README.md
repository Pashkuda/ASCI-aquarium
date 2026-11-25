# ASCII Aquarium Wallpaper

A lightweight, interactive ASCII-art live desktop wallpaper for **Windows 11 (24H2 / 25H2)** and Windows 10, built with pure **C++17, Win32, Direct3D 11, and DirectComposition** (~300 KB standalone `.exe` — no Chromium, Electron, or third-party wallpaper engines required).

```text
~≈~^~≈~~~≈~~~^~≈~~~≈~^~~~≈~~~≈~~~^~≈~~~≈~
       .-------.              ><((º>
      /  /###\  \ .-.                  _.---.
    =|  |#####|  |-(o>       ><°>   \ / ))) o\
      \__/   \__/                   / \_____./
             _.-----._
            /   o o   \       (\/) (\/)
           (___________)     / (o,,o) \
             | ~ | ~ |       //  \/  \\
~~~~_~~~~~~_~~~~~~_~~~~~~_~~~~~~_~~~~~~_~~~~
```

## Features

- **Native Windows 11 24H2 / 25H2 Desktop Embedding:** Renders directly behind desktop icons inside `Progman` using hardware-accelerated **Direct3D 11 + DXGI SwapChain + DirectComposition** (`WS_EX_NOREDIRECTIONBITMAP`) without blocking icons or stealing focus.
- **Ultra-Lightweight & Game-Friendly:** Single ~300 KB native binary with minimal CPU usage and automatic pause (`SHQueryUserNotificationState` + fullscreen detection) whenever a fullscreen game or video is running.
- **Rich Animated ASCII Ecosystem:**
  - Animated water surface waves, diagonal light rays, and rising oxygen bubbles.
  - Schooling minnows, striped clownfish, tropical fish, curved reef fish, diamond angelfish, a spiny pufferfish (inflates when startled), a gliding sea turtle, bobbing seahorses, and pulsating jellyfish.
  - Taskbar-aware seabed (`SPI_GETWORKAREA`) with lush swaying kelp, shaded rocks, branching corals, shells, a scuttling 3-row crab, and a sunken treasure chest releasing golden bubbles.
- **Interactive Desktop Physics:**
  - **Mouse Movement:** Fish react and scatter from quick cursor sweeps, pufferfish inflate, the crab raises its claws and sidesteps, seaweed sways with the cursor current, and the treasure chest opens when hovered.
  - **Click to Feed:** A quick left-click on empty desktop drops sinking ASCII food pellets (`•`, `·`, `*`) with a water ripple — nearby fish turn around and swim to eat them.
- **System Tray Controls:**
  - Feed fish
  - 5 built-in color themes (*Deep Ocean*, *Matrix Green*, *Retro Amber*, *Neon Cyberpunk*, *Monochrome*)
  - Toggle mouse interaction, game auto-pause, pause/resume
  - One-click Windows startup toggle (`HKCU\...\Run`)

## Usage

- **Run as live desktop wallpaper:**
  ```powershell
  .\AsciiAquariumWallpaper.exe
  ```
- **Run in windowed preview mode:**
  ```powershell
  .\AsciiAquariumWallpaper.exe --window
  ```
  Windowed shortcuts: `Space` (feed fish), `T` (cycle color theme), `Esc` (exit).

## Building from Source

Requires **MinGW-w64 (`g++`)** with C++17 and Windows SDK libraries:

```powershell
g++ -std=c++17 -O2 -s -Wall -Wextra -mwindows -municode -static -static-libgcc -static-libstdc++ `
    src/main.cpp src/aquarium.cpp `
    -o AsciiAquariumWallpaper.exe `
    -lgdi32 -luser32 -lshell32 -ladvapi32 -lcomctl32 -ld3d11 -ldxgi -ldcomp -lole32
```

## Project Structure

```text
src/
├── main.cpp       # Win32 host, DirectComposition wallpaper embedding, tray menu, auto-pause
├── aquarium.h     # ASCII aquarium entity definitions and simulation interface
├── aquarium.cpp   # Simulation physics, cursor interaction, and multi-line ASCII rendering
└── themes.h       # 5 curated color palettes
```
