#include "skeletal/nt_skeletal.h"

#include <stdint.h>

/*
 * The track clock lives alone in this translation unit so that a game which
 * only advances clocks — a baked-bank character, for instance — links no
 * sampler and no clip code.
 */

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
void nt_skeletal_tracks_advance(nt_skeletal_track_t *tracks, uint32_t count, double dt) {
    NT_ASSERT(tracks != NULL);
    NT_ASSERT(dt >= 0.0);

    for (uint32_t i = 0; i < count; ++i) {
        nt_skeletal_track_t *track = &tracks[i];
        if ((track->flags & NT_SKELETAL_TRACK_OCCUPIED) == 0U) {
            continue;
        }
        NT_ASSERT(track->duration >= 0.0);
        /* x - x rejects NaN and infinity without libm: a non-finite speed makes
         * the int64 cast of the cycle count undefined and traps on wasm. */
        NT_ASSERT((track->speed - track->speed) == 0.0F);

        if (track->duration == 0.0) {
            track->time = 0.0;
            continue;
        }

        double time = track->time + ((double)track->speed * dt);
        if ((track->flags & NT_SKELETAL_TRACK_LOOPING) != 0U) {
            /* Floor/modulo rather than repeated subtraction: reverse playback
             * and a step spanning several cycles both normalize in one go.
             * Truncate-then-correct floor keeps the module free of libm. */
            const double cycles = time / track->duration;
            double whole = (double)(int64_t)cycles;
            if (whole > cycles) {
                whole -= 1.0;
            }
            time -= whole * track->duration;
            /* The quotient and the product round, so an exact cycle boundary
             * can come back as duration or a hair below zero; the cycle starts
             * over at 0 either way. */
            if (time < 0.0 || time >= track->duration) {
                time = 0.0;
            }
        } else if (time < 0.0) {
            time = 0.0;
        } else if (time > track->duration) {
            time = track->duration;
        }
        track->time = time;
    }
}
