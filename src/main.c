#include <stdio.h>
#include <stdbool.h>
#include <SDL2/SDL.h>
#include "log.h"
#include "cartridge.h"
#include "cpu.h"
#include "cpu_test.h"
#include "ppu.h"

#define MODULE "Main"

int
main(int argc, char **arv) {
    SDL_Window *window = NULL;
    SDL_Renderer *renderer = NULL;
    SDL_Texture *texture = NULL;
    SDL_Event e;
    Uint32 start, elapsed;
    bool success, looping, paused;

    log_init();
    cpu_init();
    cpu_test_init();
    cartridge_init();
    ppu_init();

    log_set_level(LOG_LEVEL_DEBUG);

    success = log_open();
    if (!success) {
        fprintf(stderr, "Failed to open log file: %s", log_get_error());
    }

    if (success) {
        if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO) < 0) {
            log_err("Could not initialize SDL: %s", SDL_GetError());
            success = false;
        }
    }

    if (success) {
        window = SDL_CreateWindow("NES", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED, 256, 240, SDL_WINDOW_SHOWN);
        if (window == NULL) {
            log_err(MODULE, "Failed to create SDL Window: %s", SDL_GetError());
            success = false;
        }
    }

    if (success) {
        renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED);
        if (renderer == NULL) {
            log_err(MODULE, "Failed to create SDL renderer: %s", SDL_GetError());
            success = false;
        }
    }

    if (success) {
        texture = SDL_CreateTexture (renderer, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, 256, 240);
        if (texture == NULL) {
            log_err(MODULE, "Failed to create SDL texture: %s", SDL_GetError());
            success = false;
        }
    }

    if (success) {
        //success = cartridge_load("../../roms/nestest.nes");
        success = cartridge_load("../../roms/donkey_kong.nes");
        //success = cartridge_load("../../roms/scanline/scanline.nes");
        if (success) {
            cpu_power();
        }
    }

    if (success) {
        ppu_set_texture(texture);

        looping = true;
        paused = false;

        while (success && looping) {
            start = SDL_GetTicks();

            while (SDL_PollEvent(&e) != 0) {
                switch (e.type) {
                    case SDL_KEYDOWN:
                        switch (e.key.keysym.sym) {
                            case SDLK_p:
                                paused = !paused;
                                break;
                            default:
                                break;
                        }

                        break;
                    case SDL_QUIT:
                        looping = false;
                        break;
                }
            }

            if (paused) {
                SDL_Delay(100);
                break;
            }

            cpu_run_frame();

            SDL_RenderClear(renderer);
            SDL_RenderCopy(renderer, texture, NULL, NULL);
            SDL_RenderPresent(renderer);

            //make sure we're running at 60FPS
            elapsed = SDL_GetTicks() - start;
            if (elapsed < (1000.0 / 60.0)) {
                SDL_Delay((1000.0 / 60.0) - elapsed);
            }
        }
    }

    if (texture != NULL) {
        SDL_DestroyTexture(texture);
    }
    if (renderer != NULL) {
        SDL_DestroyRenderer(renderer);
    }
    if (window != NULL) {
        SDL_DestroyWindow(window);
    }
    log_close();

    SDL_Quit();
    cartridge_free();
    cpu_free();
    cpu_test_free();
    ppu_free();
    log_free();

    return 0;
}