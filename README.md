# DreamWorks Shrek 2 Remastered — CrossOver / Wine `GameInput.dll` Fix

Fixes the startup crash of **DreamWorks Shrek 2 Remastered** under **CrossOver / Wine / Proton** (macOS / Linux) with full **gamepad and rumble support**.

---

## Root Cause

On CrossOver / Wine, the game initializes Direct3D 12 (via D3DMetal) successfully, then immediately crashes with a **BugSplat** dialog and the following trace in `logs/Shrek-*.log`:

```text
GAME CRASH
Code: 0xC0000005
Address: 00000001402FAF11
W:\BuildAgent\work\swamp2_p4\Code\LuxEngine2\Input\Win64\LuxWindowsPlatformInput.cpp(40): Lux::Input::LuxWindowsPlatformInput::LuxWindowsPlatformInput
```

**Why this happens:**
1. The game uses Microsoft's **GameInput v1** API (`GameInputCreate` from `GameInput.dll`).
2. Wine / CrossOver ships a stub `gameinput.dll` whose `GameInputCreate` returns `0x80004001` (`E_NOTIMPL`).
3. `Shrek.exe` skips `QueryInterface` when `GameInputCreate` fails, leaving its `IGameInput*` pointer as `NULL`, and then immediately calls `IGameInput::SetFocusPolicy` without a null check → `0xC0000005` (Access Violation).

---

## How This Fix Works

Instead of patching `Shrek.exe`, this repository provides a lightweight native **`GameInput.dll`** shim (~7 KB, depends only on `KERNEL32.dll`) that:
- Implements the **GameInput v1** COM interfaces used by the game (`GameInputCreate`, `IGameInput`, `IGameInputDevice`, `IGameInputReading`).
- Translates gamepad input (buttons, D-Pad, left/right thumbsticks, LT/RT triggers), hot-plugging for up to 4 controllers, and vibration (`SetRumbleState`) to standard **`XINPUT1_4.dll`**, which works out of the box in CrossOver / Wine / Proton.
- Preserves standard keyboard and mouse input (handled via Win32 APIs).

> Note: Virtual controllers report an Xbox `vendorId` (`0x045E`), so in-game button prompts will display Xbox icons.

---

## Installation

### 1. Place `GameInput.dll` next to `Shrek.exe`
Download [`bin/GameInput.dll`](bin/GameInput.dll) (or grab it from the [Releases](https://github.com/cloverfield11/shrek2-remastered-crossover-fix/releases) page) and copy it into the game's directory alongside `Shrek.exe`, for example:

```text
drive_c/Games/DreamWorks Shrek 2 Remastered/GameInput.dll
```

### 2. Enable the `gameinput` DLL override
Without a DLL override, Wine will load its built-in stub from `system32` instead of the `GameInput.dll` next to `Shrek.exe`.

#### Option A: CrossOver GUI (`winecfg`)
1. Select your bottle in CrossOver and open **Wine Configuration** (`winecfg`).
2. Go to the **Libraries** tab.
   *(Optional: on the **Applications** tab, add `Shrek.exe` first if you want this override to apply only to the game.)*
3. In **New override for library**, type:
   ```text
   gameinput
   ```
4. Click **Add** and verify it is set to **`(native, builtin)`**.
5. Click **Apply** → **OK**.

#### Option B: macOS Terminal (CrossOver)
Replace `"1243"` with your bottle name (and adjust the `.app` path if you use standard `CrossOver.app` instead of `CrossOver Preview.app`):

```bash
"/Applications/CrossOver Preview.app/Contents/SharedSupport/CrossOver/bin/wine" --bottle "1243" reg add "HKCU\\Software\\Wine\\AppDefaults\\Shrek.exe\\DllOverrides" /v gameinput /t REG_SZ /d "native,builtin" /f
```

#### Option C: Steam / Proton (Linux / Steam Deck)
Set the game's **Launch Options** in Steam to:

```text
WINEDLLOVERRIDES="gameinput=n,b" %command%
```

---

## Building from Source

Requires `mingw-w64` (`x86_64-w64-mingw32-gcc`).

On macOS (Homebrew):
```bash
brew install mingw-w64
./build.sh
```

On Linux (Debian/Ubuntu):
```bash
sudo apt install gcc-mingw-w64-x86-64
./build.sh
```

The compiled DLL will be output to `bin/GameInput.dll`.

---

## Repository Structure

- `src/gameinput_shim.c` — `GameInput v1` to `XInput` shim implementation
- `src/gameinput.def` — DLL export definitions (`GameInputCreate @1`)
- `build.sh` — build script using `x86_64-w64-mingw32-gcc`
- `bin/GameInput.dll` — prebuilt 64-bit DLL

---

## License

MIT License — see [LICENSE](LICENSE).
