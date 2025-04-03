#include <SFML/Graphics.h>
#include <stdio.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define MAX_ITER 256
#define ESCAPE_RADIUS 10.0
#define WIDTH 800
#define HEIGHT 600
#define FILENAME "mandelbrot_saves.txt"

typedef struct {
    double center_x;
    double center_y;
    double scale;
    int color_formula;
} MandelbrotState;

// Глобальные флаги (сохранены как в оригинале)
int graphics_enabled = 1;
int run_count = 1;

sfColor get_color(int iterations) {
    if (iterations == MAX_ITER) {
        return sfBlack;
    } else {
        float t = (float)iterations / MAX_ITER;
        return sfColor_fromRGB(
            (sfUint8)(9 * (1 - t) * t * t * t * 255),
            (sfUint8)(15 * (1 - t) * (1 - t) * t * t * 255),
            (sfUint8)(8.5 * (1 - t) * (1 - t) * (1 - t) * t * 255)
        );
    }
}

double compute_mandelbrot_optimized(sfUint8* pixels, const MandelbrotState* state) {
    clock_t start = clock(); // Начало замера ВЫЧИСЛЕНИЙ
    
    int* iterations = malloc(WIDTH * HEIGHT * sizeof(int));
    if (!iterations) return 0.0;

    // Основные вычисления (только эта часть замеряется)
    for (int r = 0; r < run_count; r++) {
        for (int y = 0; y < HEIGHT; y++) {
            for (int x = 0; x < WIDTH; x += 4) {
                double cx[4], cy[4];
                for (int k = 0; k < 4; k++) {
                    cx[k] = state->center_x + (x + k - WIDTH/2.0) * state->scale;
                    cy[k] = state->center_y + (y - HEIGHT/2.0) * state->scale;
                }

                double zx[4] = {0}, zy[4] = {0};
                int iter[4] = {0};
                int mask = 0;

                for (int i = 0; i < MAX_ITER && mask != 0x0F; i++) {
                    for (int k = 0; k < 4; k++) {
                        if (mask & (1 << k)) continue;

                        double zx2 = zx[k] * zx[k];
                        double zy2 = zy[k] * zy[k];
                        double zxzy = 2 * zx[k] * zy[k];

                        zx[k] = zx2 - zy2 + cx[k];
                        zy[k] = zxzy + cy[k];

                        if (zx2 + zy2 > ESCAPE_RADIUS * ESCAPE_RADIUS) {
                            mask |= (1 << k);
                            iter[k] = i;
                        }
                    }
                }

                for (int k = 0; k < 4 && (x + k) < WIDTH; k++) {
                    iterations[y*WIDTH + x + k] = iter[k];
                }
            }
        }
    }

    clock_t end = clock(); // Конец замера ВЫЧИСЛЕНИЙ
    double compute_time = (double)(end - start) / CLOCKS_PER_SEC;

    // Отрисовка (НЕ входит в замер времени)
    if (graphics_enabled && pixels) {
        for (int i = 0; i < WIDTH*HEIGHT; i++) {
            sfColor color = get_color(iterations[i]);
            pixels[4*i]   = color.r;
            pixels[4*i+1] = color.g;
            pixels[4*i+2] = color.b;
            pixels[4*i+3] = 255;
        }
    }

    free(iterations);
    return compute_time;
}

void print_usage() {
    printf("Usage:\n");
    printf("  --graphics       Enable graphics mode (default)\n");
    printf("  --no-graphics    Disable graphics, compute only\n");
    printf("  --runs=N        Number of computation runs per point (default=1)\n");
}

int parse_args(int argc, char* argv[]) {
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--graphics") == 0) {
            graphics_enabled = 1;
        } else if (strcmp(argv[i], "--no-graphics") == 0) {
            graphics_enabled = 0;
        } else if (strncmp(argv[i], "--runs=", 7) == 0) {
            run_count = atoi(argv[i] + 7);
            if (run_count < 1) run_count = 1;
        } else {
            printf("Unknown option: %s\n", argv[i]);
            print_usage();
            return 0;
        }
    }
    return 1;
}

int main(int argc, char* argv[]) {
    if (!parse_args(argc, argv)) return 1;

    sfRenderWindow* window = NULL;
    sfTexture* texture = NULL;
    sfSprite* sprite = NULL;
    sfUint8* pixels = NULL;
    sfFont* font = NULL;
    sfText* fpsText = NULL;
    sfClock* fpsClock = NULL;

    if (graphics_enabled) {
        window = sfRenderWindow_create(
            (sfVideoMode){WIDTH, HEIGHT, 32},
            "Mandelbrot Set (Optimized)",
            sfClose, NULL
        );
        if (!window) return 1;

        texture = sfTexture_create(WIDTH, HEIGHT);
        sprite = sfSprite_create();
        sfSprite_setTexture(sprite, texture, sfTrue);

        pixels = malloc(WIDTH * HEIGHT * 4);
        font = sfFont_createFromFile("Roboto-Italic-VariableFont_wdth,wght.ttf");
        fpsText = sfText_create();
        sfText_setFont(fpsText, font);
        sfText_setCharacterSize(fpsText, 20);
        sfText_setFillColor(fpsText, sfWhite);
        sfText_setPosition(fpsText, (sfVector2f){10, 10});

        fpsClock = sfClock_create();
    }

    MandelbrotState state = {-0.5, 0.0, 0.005, 0};
    int frameCount = 0;
    float fps = 0;

    while (graphics_enabled ? sfRenderWindow_isOpen(window) : frameCount < 1) {
        if (graphics_enabled) {
            sfEvent event;
            while (sfRenderWindow_pollEvent(window, &event)) {
                if (event.type == sfEvtClosed) sfRenderWindow_close(window);
                if (event.type == sfEvtKeyPressed) {
                    switch (event.key.code) {
                        case sfKeyZ: state.scale *= 0.5; break;
                        case sfKeyX: state.scale *= 2.0; break;
                        case sfKeyLeft:  state.center_x -= 50 * state.scale; break;
                        case sfKeyRight: state.center_x += 50 * state.scale; break;
                        case sfKeyUp:    state.center_y -= 50 * state.scale; break;
                        case sfKeyDown:  state.center_y += 50 * state.scale; break;
                        default: break;
                    }
                }
            }
        }

        // Замер и выполнение вычислений
        double compute_time = compute_mandelbrot_optimized(pixels, &state);
        frameCount++;

        if (graphics_enabled) {
            // Обновление FPS
            if (sfTime_asSeconds(sfClock_getElapsedTime(fpsClock)) >= 1.0f) {
                fps = frameCount;
                frameCount = 0;
                sfClock_restart(fpsClock);
                char fpsStr[64];
                snprintf(fpsStr, sizeof(fpsStr), "FPS: %.1f | Compute: %.2fms (Runs: %d)",
                        fps, compute_time*1000, run_count);
                sfText_setString(fpsText, fpsStr);
            }

            // Отрисовка
            sfTexture_updateFromPixels(texture, pixels, WIDTH, HEIGHT, 0, 0);
            sfRenderWindow_clear(window, sfBlack);
            sfRenderWindow_drawSprite(window, sprite, NULL);
            sfRenderWindow_drawText(window, fpsText, NULL);
            sfRenderWindow_display(window);
        } else {
            printf("Compute time: %.3f sec (Runs: %d)\n", compute_time, run_count);
            break;
        }
    }

    // Очистка
    if (graphics_enabled) {
        free(pixels);
        sfText_destroy(fpsText);
        sfFont_destroy(font);
        sfClock_destroy(fpsClock);
        sfSprite_destroy(sprite);
        sfTexture_destroy(texture);
        sfRenderWindow_destroy(window);
    }

    return 0;
}