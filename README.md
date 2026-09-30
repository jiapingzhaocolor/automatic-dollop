# FilmGrainOFX

A CPU OpenFX filter for DaVinci Resolve (and other OFX hosts) adapted from the physically motivated stochastic film-grain model in **Realistic Film Grain Rendering** by Newson, Faraj, Galerne and Delon.

This repository is intentionally set up so you do **not** need Visual Studio or Xcode on your own machine. GitHub Actions builds the `.ofx.bundle` for Windows, macOS and Linux, runs deterministic core and binary-loading OFX lifecycle tests, packages each build, and gives you downloadable artifacts.

## What was changed from the uploaded command-line program

The original program reads PNG/TIFF files, converts channels into an internal matrix, normalises them, runs a Monte-Carlo film-grain renderer, then writes a new image. An OFX plug-in must instead accept frame buffers supplied by Resolve and write directly into the host-provided output buffer.

This port therefore:

- removes PNG/TIFF file I/O and the command-line `main()`;
- adapts the **pixel-wise** stochastic grain algorithm directly to OFX float image buffers;
- keeps the Poisson/Boolean grain model and optional log-normal grain-radius variation;
- makes the random sequence deterministic so Resolve's frame cache does not change every time the same frame is re-rendered;
- adds a frame-dependent seed when **Animated** is enabled;
- fixes the 256-entry lookup-table overrun in the uploaded pixel-wise implementation;
- applies the integration Gaussian width once rather than twice in the uploaded pixel-wise path;
- adds OFX-friendly **Mix**, **Colour Grain**, **Animated**, and **Seed** controls;
- preserves alpha;
- disables tiled input because this model samples neighbouring spatial cells;
- uses OpenFX 1.5.1 headers pinned at build time.

The grain-wise implementation from the original source is not used in v0.1 because it creates full Monte-Carlo boolean images and uses non-deterministic `random_device` calls; that is a poor fit for interactive OFX rendering and Resolve caching. The physically motivated pixel-wise implementation is the practical starting point for the plug-in.

## Fix for Resolve reporting "OFX Plugin is not available" (v0.3)

The previous wrapper returned `kOfxStatReplyDefault` for `OfxActionCreateInstance`.
Resolve loaded the binary but logged `Create instance failed`, then displayed the
unavailable-plugin / no-parameters message. The wrapper now explicitly accepts
creation and destruction. The plugin identifier is unchanged, and the minor
version is increased to 3. Windows MSVC builds also statically link the C++ runtime.

GitHub now loads the built `.ofx` in a small test host and checks discovery,
description, all eight controls, instance creation/destruction and unload before
uploading a bundle. This is a regression check, not a substitute for testing in Resolve.
See the [OpenFX action reference](https://openfx.readthedocs.io/en/main/Reference/ofxImageEffectActions.html).

### Updating your installed Windows build

1. Commit and push these source changes, including `tests/ofx_lifecycle_smoke.cpp`.
2. In GitHub Actions, run **Build OpenFX bundles** on the updated branch and wait
   for the Windows build and both tests to pass.
3. Download **FilmGrainOFX-windows-x64** and extract both ZIP layers.
4. Close Resolve. Replace the installed `FilmGrainOFX.ofx.bundle` folder in
   `C:\Program Files\Common Files\OFX\Plugins\` with the complete new bundle.
   The binary must be at `FilmGrainOFX.ofx.bundle\Contents\Win64\FilmGrainOFX.ofx`.
5. Restart Resolve and add **Film Grain** to a fresh test node. You should see
   Amount, Grain Size, Size Variation, Softness, Quality Samples, Color Grain,
   Animated and Seed.

If a saved node refers to the older `StochasticFilmGrain` identifier, replace that
old effect with **Film Grain**; it is a different plugin identity. If the current
Film Grain still fails, check the latest `Create instance failed` / OpenFX entries
in `%APPDATA%\Blackmagic Design\DaVinci Resolve\Support\logs\ResolveDebug.txt`
and verify that the installed binary is from the new workflow run.

## Build on GitHub

1. Create a new GitHub repository.
2. Upload/push this entire folder. **Keep `.github/workflows/build.yml` exactly where it is.**
3. Open the repository's **Actions** tab.
4. Select **Build OpenFX bundles**.
5. Click **Run workflow**.
6. When the run finishes, download the artifact for your platform:
   - `FilmGrainOFX-windows-x64`
   - `FilmGrainOFX-macos-universal`
   - `FilmGrainOFX-linux-x86_64`
7. GitHub gives you an artifact ZIP containing a platform ZIP. Extract both layers until you have:

```text
FilmGrainOFX.ofx.bundle/
  Contents/
    Win64/FilmGrainOFX.ofx        # Windows
    MacOS/FilmGrainOFX.ofx        # macOS universal
    Linux-x86-64/FilmGrainOFX.ofx # Linux
```

OpenFX specifies this bundle/architecture layout, and the workflow creates it automatically.

## Install in DaVinci Resolve

### Windows

Copy the complete folder:

```text
FilmGrainOFX.ofx.bundle
```

to:

```text
C:\Program Files\Common Files\OFX\Plugins\
```

You can also open an elevated PowerShell in the extracted build and run:

```powershell
powershell -ExecutionPolicy Bypass -File scripts\install_windows.ps1 -BundlePath .\FilmGrainOFX.ofx.bundle
```

### macOS

Copy the bundle to:

```text
/Library/OFX/Plugins/
```

or run:

```bash
./scripts/install_macos.sh ./FilmGrainOFX.ofx.bundle
```

The script also clears quarantine on the unsigned local build. This build is ad-hoc/local development software, not notarized for public distribution.

### Linux

Copy the bundle to:

```text
/usr/OFX/Plugins/
```

or run:

```bash
./scripts/install_linux.sh ./FilmGrainOFX.ofx.bundle
```

Restart Resolve after installation. In Resolve, look under **Open FX → Film Grain → Film Grain**.

## Controls

| Control | Meaning |
|---|---|
| Grain Size | Mean radius of simulated grains, in current render pixels. Source CLI default: `0.1`. |
| Size Variation | Standard deviation of grain radius as a fraction of the mean. `0` gives fixed-radius grains. |
| Softness | Gaussian integration jitter. Source CLI default: `0.8`. |
| Quality Samples | Monte-Carlo samples per output pixel. Source CLI default: `800`; plug-in default: `64` for usability. |
| Amount | `0` = source, `1` = full stochastic rendering. |
| Color Grain | Independent R/G/B stochastic renders. Off uses a luma grain delta while retaining source chroma. |
| Animated | Changes the deterministic seed per frame. |
| Seed | Base grain pattern seed. |

## Recommended Resolve workflow

The stochastic model expects image values in the approximately **0-1** range. Resolve can carry scene-linear/HDR values outside that range, so the plug-in clamps the values used to calculate grain density. For predictable results, use the effect on a display-referred or otherwise bounded 0-1 image, or deliberately place it after the transform that brings your working image into that range.

For grading:

1. Start with `Quality Samples = 16-64` while tuning the look.
2. Use `Grain Size` around `0.05-0.25` as an initial exploration range.
3. Increase `Quality Samples` for the final render. `200-800` is closer to the original command-line quality intent, but it is computationally heavy.
4. Use Resolve render cache / node cache for final-quality sample counts.

This is a Monte-Carlo physical grain model, not a cheap procedural noise overlay. High samples at UHD/4K can be very slow on CPU.

## Local build (optional)

Requirements: CMake 3.24+, a C++17 compiler, and internet access during CMake configure so it can fetch the pinned OpenFX headers.

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=ON
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

On Windows with Visual Studio generators:

```powershell
cmake -S . -B build -A x64 -DBUILD_TESTING=ON
cmake --build build --config Release --parallel
ctest --test-dir build -C Release --output-on-failure
```

The finished bundle is under `build/dist/FilmGrainOFX.ofx.bundle`.

## Current limitations

- CPU only.
- Supports 8-bit, 16-bit and float RGB/RGBA buffers.
- The original grain-wise renderer is not exposed yet.
- Grain size is interpreted in pixels of the current render scale; proxy playback can therefore look different from full-resolution final output.
- The algorithm is expensive at high sample counts.

A sensible v0.2 would port the pixel-wise sampler to CUDA/OpenCL/Metal or write a Resolve-compatible GPU path. That would make the model much more practical for 4K work.

## License and attribution

GPL-3.0-or-later. See `LICENSE` and `THIRD_PARTY_NOTICES.md`.
