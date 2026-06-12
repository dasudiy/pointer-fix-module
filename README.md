# Zygisk Pointer Capture Fix

A Zygisk module to fix the Pointer Capture axis swap and direction inversion bug in Android 13 and above.

## The Problem

On certain Android devices, TV boxes, tablets, or custom ROMs running Android 13/14+, enabling **Pointer Capture** (used for relative mouse input in games or desktop emulation) causes the mouse coordinates to be rotated by 90 degrees. 

Specifically, the system transforms the relative mouse input `(x, y)` to `(-y, -x)`. This results in:
* Moving the mouse horizontally moves the cursor vertically.
* Axis directions are inverted (e.g., moving left moves the camera down/up).

This bug affects native games and remote streaming apps like **Moonlight**, **Steam Link**, **Geforce Now**, **Minecraft (MCPE)**, and **Termux-X11**, making them unplayable with a mouse.

## How It Works

This module injects into target applications using **Zygisk** and intercepts mouse events:
1. **JNI Hooking**: Intercepts `MotionEvent` Java/Kotlin methods (`nativeGetAxisValue` and `nativeGetSource`) to fix standard Android apps.
2. **PLT Hooking**: Intercepts native NDK APIs (`AMotionEvent_getAxisValue`, `AMotionEvent_getX`, `AMotionEvent_getY`) inside `libandroid.so` to fix pure-native C/C++ games and engines.
3. **Axis Swap & X Negation**: When an event with source `SOURCE_MOUSE_RELATIVE` (`0x00020004`) is detected, it swaps the X and Y axes and negates X only (`getX = -Y`, `getY = X`), restoring correct relative motion.

## Tested Devices

This module has only been tested on the following device (queried via `adb`):

| Property | Value |
|----------|-------|
| Model | Lenovo TB-Q706F |
| Product | LenovoTB-Q706F_EEA |
| Device codename | Q706F |
| Manufacturer | Lenovo |
| Android version | 13 (API 33) |
| Build ID | TKQ1.221013.002 |
| Build display | `TB-Q706F_S530352_240807_ROW` |
| Build incremental | `TB-Q706F_USR_S530352_2408071712_Q00050_ROW` |
| Security patch | 2024-08-05 |
| Build type | user / release-keys |
| SoC platform | kona (Qualcomm) |
| Kernel | 4.19.157-perf+ |
| ABI | arm64-v8a |
| Screen | 1600×2560 @ 240 dpi |

Other devices may use a different axis rotation variant and may need a different fix (e.g. negate both axes, or negate Y only). Feedback and pull requests for additional devices are welcome.

## Requirements

* **Root Access**: Magisk, KernelSU, or APatch.
* **Zygisk**: 
  * Enabled in Magisk settings, or
  * Using **ZygiskNext** if you are on KernelSU or APatch.

## Installation

1. Download the latest pre-compiled module zip from the [Releases](https://github.com/zygisk-pointer-fix/zygisk-pointer-fix/releases) or the GitHub Actions build artifact.
2. Open your root manager application (e.g., Magisk, KernelSU, or APatch).
3. Navigate to the **Modules** tab.
4. Choose **Install from storage** and select the downloaded zip.
5. Reboot your device.

## Building from Source

### Prerequisites
* Android NDK (r21 or higher recommended)
* Git

### Steps
1. Clone the repository recursively to fetch the lightweight `libcxx` submodule:
   ```bash
   git clone --recursive https://github.com/zygisk-pointer-fix/zygisk-pointer-fix.git
   cd zygisk-pointer-fix
   ```
2. Build the native libraries using `ndk-build`:
   ```bash
   export NDK_PROJECT_PATH=.
   ndk-build -C module
   ```
3. The compiled `.so` files will be placed under `module/libs/`.
4. Follow the layout in `.github/workflows/build.yml` to package them into a Magisk-compatible zip.

## License

This project is licensed under the [MIT License](LICENSE).
