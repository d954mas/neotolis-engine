/* miniaudio single-file implementation TU (vendored, v0.11.25).
   Web backend uses ScriptProcessorNode (AudioWorklets left off, so no
   -sASYNCIFY/-sWASM_WORKERS needed). Only WAV + MP3 decoders are kept. */
#define MINIAUDIO_IMPLEMENTATION
#define MA_NO_FLAC
#define MA_NO_VORBIS
#define MA_NO_ENCODING
#define MA_NO_GENERATION
#include "miniaudio.h"
