#ifndef BASISU_FIXTURES_H
#define BASISU_FIXTURES_H

#include "nt_basisu_transcoder.h"

typedef struct {
    const char *name;
    uint32_t w;
    uint32_t h;
    bool alpha;
    nt_basisu_codec_t codec;
    uint32_t levels;
} fixture_t;

static const fixture_t s_fixtures[] = {
    {"etc1s_96x64_rgba", 96, 64, true, NT_BASISU_CODEC_ETC1S, 7},         {"etc1s_96x64_rgb", 96, 64, false, NT_BASISU_CODEC_ETC1S, 7},
    {"etc1s_13x7_rgba", 13, 7, true, NT_BASISU_CODEC_ETC1S, 4},           {"etc1s_1x1_rgb", 1, 1, false, NT_BASISU_CODEC_ETC1S, 1},
    {"etc1s_128x128_rgba", 128, 128, true, NT_BASISU_CODEC_ETC1S, 8},     {"etc1s_4x4_rgba", 4, 4, true, NT_BASISU_CODEC_ETC1S, 3},
    {"uastc_96x64_rgba", 96, 64, true, NT_BASISU_CODEC_UASTC_LDR, 7},     {"uastc_96x64_rgb", 96, 64, false, NT_BASISU_CODEC_UASTC_LDR, 7},
    {"uastc_13x7_rgba", 13, 7, true, NT_BASISU_CODEC_UASTC_LDR, 4},       {"uastc_1x1_rgb", 1, 1, false, NT_BASISU_CODEC_UASTC_LDR, 1},
    {"uastc_128x128_rgba", 128, 128, true, NT_BASISU_CODEC_UASTC_LDR, 8}, {"uastc_4x4_rgba", 4, 4, true, NT_BASISU_CODEC_UASTC_LDR, 3},
};
#define FIXTURE_COUNT (sizeof(s_fixtures) / sizeof(s_fixtures[0]))

static const nt_texture_format_t s_targets[] = {NT_TEXTURE_FORMAT_ETC2_RGB8, NT_TEXTURE_FORMAT_ETC2_RGBA8, NT_TEXTURE_FORMAT_BC7_RGBA, NT_TEXTURE_FORMAT_ASTC_4x4_RGBA, NT_TEXTURE_FORMAT_RGBA8};
static const char *const s_target_names[] = {"etc2_rgb8", "etc2_rgba8", "bc7", "astc", "rgba8"};
#define TARGET_COUNT (sizeof(s_targets) / sizeof(s_targets[0]))

#endif
