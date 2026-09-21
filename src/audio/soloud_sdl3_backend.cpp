/*
 * Copyright (c) 2026 General Arcade (Pte. Ltd.)
 * SPDX-License-Identifier: GPL-3.0-or-later OR LicenseRef-GeneralArcade-Commercial
 */

/* SoLoud audio backend on top of SDL3.
 *
 * SoLoud ships backends for SDL1 and SDL2 only. Rather than patch its build,
 * this file provides the entry point SoLoud's `sdl2_static` backend would
 * have provided, so `Soloud::init(.., Soloud::AUTO, ..)` picks it up with no
 * changes to SoLoud itself. third_party/CMakeLists.txt compiles this instead
 * of the upstream soloud_sdl2_static.cpp.
 *
 * SDL3 replaced the pull-style audio callback with audio streams: the device
 * asks for however many bytes it wants, whenever it wants, and we push what
 * we have. SoLoud's mixer works in fixed-size blocks, so the callback loops,
 * mixing at most one block at a time.
 */

#include "soloud.h"

#include <SDL3/SDL.h>
#include <stdlib.h>

namespace SoLoud
{
    static SDL_AudioStream *gAudioStream = NULL;
    static float           *gScratch = NULL;
    static unsigned int     gChannels = 2;
    static unsigned int     gBufferFrames = 2048;

    /* Bytes one frame of interleaved float samples occupies. */
    static int frame_bytes(void)
    {
        return (int)(gChannels * sizeof(float));
    }

    static void SDLCALL soloud_sdl3_audiomixer(void *userdata, SDL_AudioStream *stream,
                                               int additional_amount, int total_amount)
    {
        (void)total_amount;
        Soloud *soloud = (Soloud *)userdata;
        if (!soloud || !gScratch) return;

        while (additional_amount > 0) {
            int frames = additional_amount / frame_bytes();
            if (frames <= 0) break;
            /* Never mix more than the block size SoLoud sized its scratch
               buffers for. */
            if (frames > (int)gBufferFrames) frames = (int)gBufferFrames;

            soloud->mix(gScratch, (unsigned int)frames);

            int bytes = frames * frame_bytes();
            SDL_PutAudioStreamData(stream, gScratch, bytes);
            additional_amount -= bytes;
        }
    }

    static void soloud_sdl3_deinit(Soloud *aSoloud)
    {
        (void)aSoloud;
        if (gAudioStream) {
            SDL_DestroyAudioStream(gAudioStream);
            gAudioStream = NULL;
        }
        free(gScratch);
        gScratch = NULL;
    }

    result sdl2static_init(Soloud *aSoloud, unsigned int aFlags, unsigned int aSamplerate,
                           unsigned int aBuffer, unsigned int aChannels)
    {
        if (!SDL_WasInit(SDL_INIT_AUDIO)) {
            if (!SDL_InitSubSystem(SDL_INIT_AUDIO))
                return UNKNOWN_ERROR;
        }

        if (aSamplerate == 0) aSamplerate = 44100;
        if (aBuffer == 0) aBuffer = 2048;
        if (aChannels == 0) aChannels = 2;

        /* Ask the device for buffers no larger than one mixing block, so the
           callback usually satisfies a request in a single mix. */
        char frames[16];
        SDL_snprintf(frames, sizeof(frames), "%u", aBuffer);
        SDL_SetHint(SDL_HINT_AUDIO_DEVICE_SAMPLE_FRAMES, frames);

        SDL_AudioSpec spec;
        SDL_zero(spec);
        spec.format = SDL_AUDIO_F32;
        spec.channels = (int)aChannels;
        spec.freq = (int)aSamplerate;

        gScratch = (float *)malloc((size_t)aBuffer * aChannels * sizeof(float));
        if (!gScratch) return OUT_OF_MEMORY;

        gChannels = aChannels;
        gBufferFrames = aBuffer;

        gAudioStream = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec,
                                                 soloud_sdl3_audiomixer, (void *)aSoloud);
        if (!gAudioStream) {
            free(gScratch);
            gScratch = NULL;
            return UNKNOWN_ERROR;
        }

        aSoloud->postinit_internal(aSamplerate, aBuffer, aFlags, aChannels);
        aSoloud->mBackendCleanupFunc = soloud_sdl3_deinit;
        aSoloud->mBackendString = "SDL3";

        SDL_ResumeAudioStreamDevice(gAudioStream);
        return SO_NO_ERROR;
    }
}
