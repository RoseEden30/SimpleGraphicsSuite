#pragma once

// Modules another loaded mod already owns. Suppressed means no hooks and no
// menu rows; the ini is left alone, so removing that mod brings them back.
namespace Compatibility
{
    enum Module : std::uint32_t
    {
        kAntiAliasing = 1 << 0,
        // Tonemap shader replacement: grading, bloom, sharpening, motion blur,
        // vignette, LUT, FSR1.
        kPostProcessing = 1 << 1,
        kReflex = 1 << 2,
    };

    bool IsSuppressed(Module a_module);

    std::string_view DetectedNames();
}
