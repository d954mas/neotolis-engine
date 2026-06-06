# nt_audio -- cross-platform audio (miniaudio backend, native + web)
# One implementation for both platforms: miniaudio handles the backend split
# internally (WASAPI on Windows, Web Audio on Emscripten).
nt_add_module(nt_audio LOG_DOMAIN "audio"
    nt_audio.c
    nt_audio.h
    nt_audio_types.h
)

target_link_libraries(nt_audio PUBLIC nt_core)
target_link_libraries(nt_audio PRIVATE nt_log miniaudio)

# Stub implementation for headless builds (CI, tests)
nt_add_module(nt_audio_stub LOG_DOMAIN "audio_stub"
    stub/nt_audio_stub.c
    nt_audio.h
    nt_audio_types.h
)

target_link_libraries(nt_audio_stub PUBLIC nt_core)
