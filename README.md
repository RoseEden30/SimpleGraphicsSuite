# Simple Graphics Suite

All-in-one SKSE graphics plugin for Skyrim SE/AE/VR.

One DLL, one ini. The plugin runs on its own. If [NativeSystemMenuFramework](https://github.com/RoseEden30/NativeSystemMenuFramework) is installed, every setting also shows up as real vanilla rows under System > Settings - optional, never required.

## Modules

- **Anti-aliasing** - TAA (with a mip LOD bias deblur), FXAA, or NVIDIA DLAA through Streamline. Mutually exclusive; picking one turns the others off.
- **Post-processing** - doodlum's Vanilla HDR tonemap with five selectable curves, FidelityFX RCAS sharpening, exposure/contrast/saturation, a progressive downsample/upsample bloom, `.cube` LUT color grading, motion blur off the engine's own motion vector buffer, and a vignette that can be limited to sneaking.
- **Upscaling (experimental)** - renders the 3D scene below native resolution through the engine's dynamic resolution, then reconstructs with FidelityFX FSR1 (EASU). Ignored under FXAA and DLAA, which both take the resolution back.
- **Accessibility** - protanopia, deuteranopia and tritanopia daltonization with adjustable strength, applied over the finished frame so it corrects the UI too.
- **NVIDIA Reflex** - low-latency mode and an optional frame cap through NVAPI. No effect on AMD or Intel.

Anything switched off costs nothing at runtime.

## Building

Visual Studio 2022, CMake 3.21+, vcpkg with `VCPKG_ROOT` set.

```
git clone https://github.com/RoseEden30/SimpleGraphicsSuite
cd SimpleGraphicsSuite
cmake --preset release
cmake --build build/release
```

The third-party SDKs under `extern/` are not stored in this repository. Fetch
them before configuring:

- `extern/CommonLibSSE-NG` - `git clone --recurse-submodules https://github.com/alandtse/CommonLibSSE-NG.git extern/CommonLibSSE-NG`
- `extern/nvapi` - the NVIDIA NVAPI SDK (`nvapi.h`, headers, `amd64/nvapi64.lib`)
- `extern/Streamline` - an NVIDIA Streamline SDK release, keeping its `include/`
  and `bin/x64/` layout

Run the two cmake commands from an **x64 Native Tools Command Prompt for
VS 2022**, so the MSVC environment is set up.

Set `SKYRIM_MODS_FOLDER` or `SKYRIM_FOLDER` to deploy on every build.

## Credits

- [doodlum](https://github.com/doodlum) - [skyrim-vanilla-hdr](https://github.com/doodlum/skyrim-vanilla-hdr) (tonemap shaders), [skyrim-lod-bias](https://github.com/doodlum/skyrim-lod-bias) (TAA deblur), [skyrim-nvidia-reflex](https://github.com/doodlum/skyrim-nvidia-reflex), and SSEShaderTools (the shader-load entry patch).
- [Community Shaders](https://github.com/doodlum/skyrim-community-shaders) - the shader-replace hook, and the Streamline DLSS integration it is modelled on: frame buffer capture, camera constants, motion vector dilation, resource tagging.
- Jorge Jimenez - the bloom chain follows his [Next Generation Post Processing in Call of Duty: Advanced Warfare](https://www.iryoku.com/next-generation-post-processing-in-call-of-duty-advanced-warfare/) (SIGGRAPH 2014).
- [alandtse](https://github.com/alandtse) - [CommonLibSSE-NG](https://github.com/alandtse/CommonLibSSE-NG), and [open-shaders](https://github.com/alandtse/open-shaders), which the jitter and post-processing hook sites were checked against.
- AMD - [FidelityFX FSR1](https://github.com/GPUOpen-Effects/FidelityFX-FSR), EASU and RCAS.
- NVIDIA - [Streamline](https://github.com/NVIDIA-RTX/Streamline) (DLAA goes through it, not the raw NGX SDK) and [NVAPI](https://github.com/NVIDIA/nvapi).
- Krzysztof Narkowicz - the [ACES fitted curve](https://knarkowicz.wordpress.com/2016/01/06/aces-filmic-tone-mapping-curve/).
- Daltonization follows Machado, Oliveira and Fialho (2009) for the deficiency simulation and Fidaner, Lin and Ozguven (2005) for the correction.

## License

GPL-3.0 with a modding exception, see [LICENSE](LICENSE) and [EXCEPTIONS.md](EXCEPTIONS.md).
