#ifndef TJ_AUDIO_ASSETS_H
#define TJ_AUDIO_ASSETS_H

/* Loose-file audio for the jam: pulls music + SFX off disk (native) or via HTTP
   (web), hands the bytes to nt_audio, and starts the music loop once the audio
   device is running. Game-side so the engine stays asset-agnostic. */

void tj_audio_assets_init(void); /* start async loads, set master volume */
void tj_audio_assets_poll(void); /* per-frame: finish loads, start music */
void tj_audio_play_click(void);  /* one-shot UI/click SFX */

#endif /* TJ_AUDIO_ASSETS_H */
