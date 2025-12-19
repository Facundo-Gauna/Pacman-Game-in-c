# Pacman Clone in C (SDL2)

Welcome to a complete, retro-style Pacman clone built in **C** using the **SDL2** library suite.
This project faithfully recreates the classic arcade experience, including ghost AI behavior, power pellets, scores, menus, and much more.

![screenshot](assets/images/menu_title.png)

---

## 🎮 Features

* Classic Pacman gameplay
* Ghost AI: Blinky, Pinky, Inky, Clyde (with original behavior logic)
* Scared ghost mode with visual flash warning
* Dot/orb collecting and scoring system
* Extra lives and reward items
* Sound effects and music (SDL_mixer)
* Custom font UI (SDL_ttf)
* Scoreboard with persistent high scores (top 10)
* User name input after game over
* Help menu, pause mode, and ranking screen

---

## 🚀 Getting Started (Cross-platform)

### 📦 Dependencies (common)

Make sure to have the following libraries available for build & runtime:

* `SDL2`
* `SDL2_ttf`
* `SDL2_image`
* `SDL2_mixer`

The project uses SDL APIs to handle time, input, sound and file paths, so keep SDL libraries & DLLs next to the executable when distributing on Windows.

---

### 🐧 Linux (Ubuntu / Debian)

Install required packages:

```bash
sudo apt update
sudo apt install build-essential pkg-config libsdl2-dev libsdl2-ttf-dev libsdl2-image-dev libsdl2-mixer-dev
```

Build & run:

```bash
make            # using the provided Makefile (uses pkg-config)
./bin/pacman
```

If your Makefile doesn’t use `pkg-config`, the equivalent gcc line is:

```bash
gcc $(pkg-config --cflags --libs sdl2 SDL2_ttf SDL2_image SDL2_mixer) -o bin/pacman src/*.c
```

---

### Windows (two recommended approaches)

#### Option A — MSYS2 / MinGW-w64 (CLI, similar to Linux)

1. Install MSYS2 from [https://www.msys2.org](https://www.msys2.org) and open **MSYS2 MinGW 64-bit** shell.
2. Install toolchain and SDL packages:

```bash
pacman -Syu
pacman -S --needed base-devel mingw-w64-x86_64-toolchain mingw-w64-x86_64-pkg-config
pacman -S mingw-w64-x86_64-SDL2 mingw-w64-x86_64-SDL2_ttf mingw-w64-x86_64-SDL2_image mingw-w64-x86_64-SDL2_mixer
```

3. From `MINGW64` shell:

```bash
make          # or compile with pkg-config as on Linux
./bin/pacman.exe
```

DLL notes: copy `SDL2.dll`, `SDL2_image.dll`, `SDL2_ttf.dll`, `SDL2_mixer.dll` into the same folder as `bin/pacman.exe` for distribution, or rely on MSYS2 environment during development.

#### Option B — Visual Studio + vcpkg (recommended for MSVC users)

1. Install Visual Studio (Desktop C++) and vcpkg.
2. With vcpkg:

```powershell
.\vcpkg install sdl2 sdl2-image sdl2-ttf sdl2-mixer
.\vcpkg integrate install
```

3. Use CMake (see `CMakeLists.txt` below or in repo):

```powershell
mkdir build
cd build
cmake .. -DCMAKE_TOOLCHAIN_FILE=C:/path/to/vcpkg/scripts/buildsystems/vcpkg.cmake
cmake --build . --config Release
```

Run the generated `pacman.exe` from `bin\` or the build output folder. Ensure SDL DLLs provided by vcpkg are shipped with your `.exe` or use a proper installer.

---

## 🔧 Build system & notes

* The repository contains a `Makefile` for Unix/MSYS2 with `pkg-config` usage. That will work in MINGW64 shells too.
* A simple `CMakeLists.txt` is recommended if you want VS + vcpkg compatibility. It should `find_package` SDL2 and the SDL2 extensions when using vcpkg.
* When compiling for Windows, either:

  * Define `SDL_MAIN_HANDLED` in the code (we currently use `#define SDL_MAIN_HANDLED`), or
  * Link with `SDL2main` (not necessary if you define `SDL_MAIN_HANDLED`).

---

## ▶️ Run

From shell:

```bash
# Linux
./bin/pacman

# Windows (MSYS2)
./bin/pacman.exe

# Windows (Explorer)
double-click bin\pacman.exe
```

---

## ⚙️ Paths, assets and saved scores (important)

* The game uses `SDL_GetBasePath()` to locate **assets** (images, fonts, sounds). That means the exe can be launched from anywhere and the app will still find assets relative to the executable.
* Scores and other user data are saved using `SDL_GetPrefPath()` (per-user application data). Example locations:

  * Windows: `C:\Users\<you>\AppData\Roaming\Facu\Pacman\scores.bin`
  * Linux: `~/.local/share/Facu/Pacman/scores.bin`
* Do **not** rely on the current working directory: place the `assets/` folder next to the executable for distribution, or pack assets into installers.

---

## 📝 High Score System

* Top 10 scores saved to the per-user `scores.bin` (via SDL_GetPrefPath).
* Players enter their name after a game ends.
* Scores persist between runs.

If you want the file in the repo (for testing), a fallback `scores.bin` in repo root can be used — but for production, the game uses the OS's per-user folder.

---

## 🖼️ Assets

* Font: [Press Start 2P](https://fonts.google.com/specimen/Press+Start+2P)
* Sprites and sounds: Inspired by original Pacman, created or adapted manually

---

## ✅ TODO / Suggestions

* Add music support (longer tracks & mixing)
* Add more reward animations
* Package as `.AppImage` or `.exe` installer
* Add level editor or multiple map support

---

Enjoy the classic arcade experience!

---
