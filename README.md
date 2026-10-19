# relbp810: Canon LBP-810 driver for 64-bit Windows

**English** | [Русский](README.ru.md)

relbp810 is an open-source driver for the Canon LBP-810 laser printer. It runs on Windows 7 SP1 through 11, in both 64-bit and 32-bit editions.

Canon released LBP-810 drivers for 32-bit Windows only, so 64-bit systems have no driver for this printer. relbp810 is a new driver built on Microsoft's Unidrv. To write it, we reverse-engineered the original Canon driver and the printer's CAPT protocol. The settings pages look the same as in Canon's driver.

## Installation

Download `LBP-810-driver-<version>.msi` from [Releases](../../releases/latest).

1. Connect the printer over USB and turn it on.
2. Run the MSI. It needs administrator rights.
3. A printer named **Canon LBP-810** appears in *Devices and Printers*.

To remove the driver, uninstall "Драйвер принтера Canon LBP-810" in *Apps* or *Programs and Features*.

> The driver package is signed with the project's self-signed certificate. So that Windows accepts the driver, the installer adds this certificate to the computer's *Trusted Root Certification Authorities* and *Trusted Publishers* stores. Uninstalling the driver removes the certificate. If you don't want to trust our certificate, build the MSI yourself. It is then signed with a certificate created on your own computer.

## Status

- **Works:** printing over USB (A4, Letter, Legal, B5, envelopes and custom sizes, at 600 or 300 dpi), toner density, toner saving, "out of paper" and other printer errors in the print queue, resending pages after an error, and Canon's settings pages.
- **Tested on:** Windows 10 x64 with a real LBP-810.
- **Not tested yet:** Windows 7, 8.1 and 11, and 32-bit Windows.
- **Not implemented yet:** Canon's printer status window, overlays, scaling and several pages per sheet.
- **UI language:** Russian only. The UI follows the Russian edition of Canon's driver.

Found a bug? Open an issue with your Windows version and a description of what happened.

## How it works

| File | Purpose |
|---|---|
| `CNLB810.GPD`, `CNLB810.INI` | Unidrv minidriver: paper sizes, resolutions and printer options |
| `CNLB810R.DLL` | Rendering plug-in: compresses each page with SCoA and writes the CAPT print job |
| `CNLB810L.DLL` | Language monitor: talks CAPT to the printer over USB, reports its status and resends pages after errors |
| `CNLB810U.DLL` | UI plug-in: Canon's three settings pages and their dialogs |
| `LBP810CA.DLL` | MSI custom actions: installs and removes the driver, the print queue and the certificate |

We studied Canon's 32-bit driver (`CAP1K.DLL`, `CAP1EMN.DLL`, `CAPMONK.DLL`, `CAP1UIK.DLL`) in a disassembler and tested what we learned about the CAPT protocol on a real printer. The code in this repository is a new implementation for Unidrv. In two places the output must match Canon's exactly: SCoA compression and the sample image on the Quality page. There our code reproduces Canon's algorithm and is checked bit for bit against the original DLLs.

## Building

You need:

- Windows 10 or 11
- Visual Studio 2022 or newer, or Build Tools for Visual Studio, with the *Desktop development with C++* workload (MSVC for x64 and x86, plus the Windows 10/11 SDK). The project is tested with Visual Studio 2026.
- Python 3.8 or newer on `PATH` (the scripts use only the standard library)
- Windows PowerShell 5.1, which comes with Windows

To build the MSI, run this in the repository root:

```bat
.\release.cmd
```

The result is `out\LBP-810-driver-<version>.msi`. `release.cmd` runs three steps, and you can also run each one on its own:

1. `build.cmd` compiles every DLL for x64 and x86. The driver goes to `out\package`, the MSI custom actions to `out\setup`, and the `captcli.exe` diagnostic tool to `out\tools`.
2. `scripts\make-package.ps1` creates the security catalog and signs the package. On the first run it creates a self-signed code signing certificate, `CN=relbp810 LBP-810 driver (self-signed)`, in your personal certificate store. Later runs reuse it.
3. `scripts\make-msi.py` packs `out\package` into the MSI.

**Canon's icons.** The icons and pictures on the settings pages belong to Canon, so they are not in this repository. `build.cmd` takes them from `CAP1UIK.DLL` in the original Canon driver. Put the original driver folder `LBP-810_R110_V110_Win_x32_RU_7` in the repository root, or set the `CANON_DRIVER` environment variable to the folder that contains `CAP1UIK.DLL`. Without these files, the build uses blank placeholders. The driver still works, but the settings pages have no icons.

**Versions.** Windows does not replace the files of a driver whose version is already installed. Before you give out a new build, increase `VER_REV` in `src\driver\version.rc.h` and `DriverVer` in `src\driver\inf\CNLB810.INF`. `make-msi.py` checks that the two match.

## Development

- To install, update or remove the driver from `out\package` without the MSI, run `scripts\install.ps1`, `scripts\update.ps1` or `scripts\uninstall.ps1` as administrator.
- `scripts\testprint.ps1` prints a test page, and `scripts\diag-install.ps1` collects installation diagnostics.
- `out\tools\<arch>\captcli.exe` talks to the printer directly. For example, `captcli status` shows the printer's status and `captcli testpage` prints a test page.
- `src\tools\dev` contains test tools that you build by hand. They take screenshots of the UI, run the language monitor outside the spooler and compare our code bit for bit with Canon's DLLs.

```
src/common    CAPT protocol, SCoA compression, print job format
src/driver    GPD, INF and the driver DLLs (render, langmon, ui, res)
src/setup     MSI custom actions
src/tools     captcli and development tools
scripts       packaging and development scripts
```

## Legal

relbp810 is an unofficial project. It is not affiliated with, endorsed by or supported by Canon Inc. Canon and LBP are trademarks of Canon Inc. This project uses them only to name the printer the driver works with.

We reverse-engineered the original driver only to make the printer work on current versions of Windows. This repository contains no Canon binaries and no Canon artwork. To look familiar, the settings dialogs copy the layout and Russian text of Canon's driver. The MSI files on the Releases page are built with the original icons.

The code is released under the [MIT License](LICENSE). It comes with no warranty, so use it at your own risk.

## Thanks

The description of the CAPT protocol from the Linux driver [captdriver](https://github.com/agalakhov/captdriver) by Alexey Galakhov was a useful reference.
