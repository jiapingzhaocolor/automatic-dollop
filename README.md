# FilmGrainOFX

A CPU OpenFX filter for DaVinci Resolve (and other OFX hosts) adapted from the physically motivated stochastic film-grain model in **Realistic Film Grain Rendering** by Newson, Faraj, Galerne and Delon.

This repository is intentionally set up so you do **not** need Visual Studio or Xcode on your own machine. GitHub Actions builds the `.ofx.bundle` for Windows, macOS and Linux, runs a deterministic core smoke test, packages each build, and gives you downloadable artifacts.

## What was changed from the uploaded command-line program

The original program reads PNG/TIFF files, converts channels into an internal matrix, normalises them, runs a Monte-Carlo film-grain renderer, then writes a new image. An OFX plug-in must instead accept frame buffers supplied by Resolve and write directly into the host-provided output buffer.

This port therefore:

- removes PNG/TIFF file I/O and the command-line `main()`;
- adapts the **pixel-wise** stochastic grain algorithm directly to OFX float image buffers;
- keeps the Poisson/Boolean grain model and optional log-normal grain-radius variation;
- makes the random sequence deterministic so Resolve's frame cache does not change every time the same frame is re-rendered;
- adds a frame-dependent seed when **Animate Grain** is enabled;
- fixes the 256-entry lookup-table overrun in the uploaded pixel-wise implementation;
- applies the integration Gaussian width once rather than twice in the uploaded pixel-wise path;
- adds OFX-friendly **Mix**, **Colour Grain**, **Animate Grain**, and **Seed** controls;
- preserves alpha;
- disables tiled input because this model samples neighbouring spatial cells;
- uses OpenFX 1.5.1 headers pinned at build time.

The grain-wise implementation from the original source is not used in v0.1 because it creates full Monte-Carlo boolean images and uses non-deterministic `random_device` calls; that is a poor fit for interactive OFX rendering and Resolve caching. The physically motivated pixel-wise implementation is the practical starting point for the plug-in.

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

Restart Resolve after installation. In Resolve, look under **Open FX → Film Emulation → Stochastic Film Grain**.

## Controls

| Control | Meaning |
|---|---|
| Grain Radius | Mean radius of simulated grains, in current render pixels. Source CLI default: `0.1`. |
| Radius Variation | Standard deviation of grain radius as a fraction of the mean. `0` gives fixed-radius grains. |
| Integration Blur | Gaussian integration jitter. Source CLI default: `0.8`. |
| Samples | Monte-Carlo samples per output pixel. Source CLI default: `800`; plug-in default: `64` for usability. |
| Mix | `0` = source, `1` = full stochastic rendering. |
| Colour Grain | Independent R/G/B stochastic renders. Off uses a luma grain delta while retaining source chroma. |
| Animate Grain | Changes the deterministic seed per frame. |
| Seed | Base grain pattern seed. |

## Recommended Resolve workflow

The stochastic model expects image values in the approximately **0-1** range. Resolve can carry scene-linear/HDR values outside that range, so the plug-in clamps the values used to calculate grain density. For predictable results, use the effect on a display-referred or otherwise bounded 0-1 image, or deliberately place it after the transform that brings your working image into that range.

For grading:

1. Start with `Samples = 16-64` while tuning the look.
2. Use `Grain Radius` around `0.05-0.25` as an initial exploration range.
3. Increase `Samples` for the final render. `200-800` is closer to the original command-line quality intent, but it is computationally heavy.
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
- Float RGB/RGBA only. This is appropriate for Resolve's normal OFX processing path but intentionally keeps v0.1 small.
- The original grain-wise renderer is not exposed yet.
- Grain size is interpreted in pixels of the current render scale; proxy playback can therefore look different from full-resolution final output.
- The algorithm is expensive at high sample counts.

A sensible v0.2 would port the pixel-wise sampler to CUDA/OpenCL/Metal or write a Resolve-compatible GPU path. That would make the model much more practical for 4K work.

## License and attribution

GPL-3.0-or-later. See `LICENSE` and `THIRD_PARTY_NOTICES.md`.
