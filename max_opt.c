#include <SFML/Graphics.h>
#include <stdio.h>
#include <stdlib.h>
#include <immintrin.h>  // For AVX2 and FMA intrinsics
#include <time.h>
#include <string.h>

#define MAX_ITER 256    // Maximum iterations per pixel
#define ESCAPE_RADIUS 10.0  // Escape radius squared
#define WIDTH 800       // Window width
#define HEIGHT 600      // Window height
#define FILENAME "mandelbrot_saves.txt"

typedef struct {
    double center_x;     // X center coordinate
    double center_y;     // Y center coordinate
    double scale;       // Zoom scale factor
    int color_formula;  // Color formula selector
} MandelbrotState;

// Global flags
int graphics_enabled = 1;
int run_count = 1;

// Convert iteration count to color
sfColor get_color(int iterations) {
    if (iterations == MAX_ITER) return sfBlack;
    
    // Smooth coloring algorithm
    float t = (float)iterations / MAX_ITER;
    return sfColor_fromRGB(
        (sfUint8)(9 * (1-t) * t*t*t * 255),      // Red component
        (sfUint8)(15 * (1-t)*(1-t) * t*t * 255), // Green component
        (sfUint8)(8.5 * (1-t)*(1-t)*(1-t) * t * 255) // Blue component
    );
}

// Compute Mandelbrot set using AVX2 and FMA instructions
double compute_mandelbrot_avx2(sfUint8* pixels, const MandelbrotState* state) {
    clock_t start = clock();
    
    int* iterations = malloc(WIDTH * HEIGHT * sizeof(int));
    if (!iterations) return 0.0;

    const __m256d escape_radius = _mm256_set1_pd(ESCAPE_RADIUS * ESCAPE_RADIUS);
    const __m256d scale = _mm256_set1_pd(state->scale);
    const __m256d width_half = _mm256_set1_pd(WIDTH / 2.0);
    const __m256d height_half = _mm256_set1_pd(HEIGHT / 2.0);
    const __m256d two = _mm256_set1_pd(2.0);

    for (int r = 0; r < run_count; r++) {
        for (int y = 0; y < HEIGHT; y++) {
            double y_offset = (y - HEIGHT/2.0) * state->scale;
            __m256d cy = _mm256_add_pd(_mm256_set1_pd(state->center_y), _mm256_set1_pd(y_offset));
            
            for (int x = 0; x < WIDTH; x += 4) {
                __m256d x_coord = _mm256_set_pd(x+3, x+2, x+1, x);
                __m256d cx = _mm256_add_pd(
                    _mm256_set1_pd(state->center_x),
                    _mm256_mul_pd(_mm256_sub_pd(x_coord, width_half), scale)
                );

                __m256d zx = cx;
                __m256d zy = cy;
                __m256d iter = _mm256_setzero_pd();
                int mask = 0xF;

                for (int i = 0; i < MAX_ITER && mask; i++) {
                    __m256d zx2 = _mm256_mul_pd(zx, zx);
                    __m256d zy2 = _mm256_mul_pd(zy, zy);
                    __m256d xy = _mm256_mul_pd(zx, zy);
                    
                    __m256d new_zx = _mm256_sub_pd(zx2, zy2);
                    new_zx = _mm256_add_pd(new_zx, cx);
                    
                    __m256d new_zy = _mm256_mul_pd(xy, two);
                    new_zy = _mm256_add_pd(new_zy, cy);
                    
                    zx = new_zx;
                    zy = new_zy;

                    __m256d norm = _mm256_add_pd(_mm256_mul_pd(zx, zx), _mm256_mul_pd(zy, zy));
                    mask = _mm256_movemask_pd(_mm256_cmp_pd(norm, escape_radius, _CMP_LT_OS));
                    
                    // Правильное преобразование маски
                    __m256d mask_vec = _mm256_castsi256_pd(
                        _mm256_setr_epi64x(
                            (mask & 0x1) ? ~0ULL : 0,
                            (mask & 0x2) ? ~0ULL : 0,
                            (mask & 0x4) ? ~0ULL : 0,
                            (mask & 0x8) ? ~0ULL : 0
                        )
                    );
                    iter = _mm256_add_pd(iter, _mm256_and_pd(_mm256_set1_pd(1.0), mask_vec));
                }

                double iter_result[4];
                _mm256_storeu_pd(iter_result, iter);
                
                for (int k = 0; k < 4 && (x + k) < WIDTH; k++) {
                    iterations[y*WIDTH + x + k] = (int)iter_result[k];
                }
            }
        }
    }

    clock_t end = clock();
    double compute_time = (double)(end - start) / CLOCKS_PER_SEC;

    if (graphics_enabled && pixels) {
        for (int i = 0; i < WIDTH * HEIGHT; i++) {
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
    printf("Mandelbrot Set Renderer (AVX2+FMA Optimized)\n");
    printf("Usage:\n");
    printf("  --graphics       Enable graphics mode (default)\n");
    printf("  --no-graphics    Disable graphics, compute only\n");
    printf("  --runs=N        Number of computation runs per point (default=1)\n");
    printf("\nControls in graphics mode:\n");
    printf("  Z/X         Zoom in/out\n");
    printf("  Arrow keys  Move view\n");
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

    // Initialize SFML objects
    sfRenderWindow* window = NULL;
    sfTexture* texture = NULL;
    sfSprite* sprite = NULL;
    sfUint8* pixels = NULL;
    sfFont* font = NULL;
    sfText* fpsText = NULL;
    sfClock* fpsClock = NULL;

    if (graphics_enabled) {
        // Create SFML window
        window = sfRenderWindow_create(
            (sfVideoMode){WIDTH, HEIGHT, 32},
            "Mandelbrot Set (AVX2+FMA Optimized)",
            sfClose, NULL
        );
        if (!window) return 1;

        // Create texture and sprite
        texture = sfTexture_create(WIDTH, HEIGHT);
        if (!texture) return 1;
        
        sprite = sfSprite_create();
        sfSprite_setTexture(sprite, texture, sfTrue);

        // Pixel buffer (RGBA format)
        pixels = malloc(WIDTH * HEIGHT * 4);
        if (!pixels) return 1;
        memset(pixels, 0, WIDTH * HEIGHT * 4);

        // FPS counter setup
        font = sfFont_createFromFile("Roboto-Italic-VariableFont_wdth,wght.ttf");
        fpsText = sfText_create();
        sfText_setFont(fpsText, font);
        sfText_setCharacterSize(fpsText, 20);
        sfText_setFillColor(fpsText, sfWhite);
        sfText_setPosition(fpsText, (sfVector2f){10, 10});

        fpsClock = sfClock_create();
    }

    // Initial Mandelbrot state
    MandelbrotState state = {-0.5, 0.0, 0.005, 0};
    int frameCount = 0;
    float fps = 0;

    // Main loop
    while (graphics_enabled ? sfRenderWindow_isOpen(window) : frameCount < 1) {
        if (graphics_enabled) {
            // Handle events
            sfEvent event;
            while (sfRenderWindow_pollEvent(window, &event)) {
                if (event.type == sfEvtClosed)
                    sfRenderWindow_close(window);
                if (event.type == sfEvtKeyPressed) {
                    switch (event.key.code) {
                        case sfKeyZ: state.scale *= 0.5; break; // Zoom in
                        case sfKeyX: state.scale *= 2.0; break; // Zoom out
                        case sfKeyLeft:  state.center_x -= 50 * state.scale; break;
                        case sfKeyRight: state.center_x += 50 * state.scale; break;
                        case sfKeyUp:    state.center_y -= 50 * state.scale; break;
                        case sfKeyDown:  state.center_y += 50 * state.scale; break;
                        default: break;
                    }
                }
            }
        }

        // Compute Mandelbrot set and measure time
        double compute_time = compute_mandelbrot_avx2(pixels, &state);
        frameCount++;

        if (graphics_enabled) {
            // Update FPS counter every second
            if (sfTime_asSeconds(sfClock_getElapsedTime(fpsClock)) >= 1.0f) {
                fps = frameCount / sfTime_asSeconds(sfClock_getElapsedTime(fpsClock));
                frameCount = 0;
                sfClock_restart(fpsClock);
                
                // Update FPS text
                char fpsStr[128];
                snprintf(fpsStr, sizeof(fpsStr), 
                        "FPS: %.1f | Compute: %.2fms (Runs: %d)\n"
                        "Pos: (%.5f, %.5f) | Scale: %.2e",
                        fps, compute_time*1000, run_count,
                        state.center_x, state.center_y, state.scale);
                sfText_setString(fpsText, fpsStr);
            }

            // Update texture and render
            sfTexture_updateFromPixels(texture, pixels, WIDTH, HEIGHT, 0, 0);
            sfRenderWindow_clear(window, sfBlack);
            sfRenderWindow_drawSprite(window, sprite, NULL);
            sfRenderWindow_drawText(window, fpsText, NULL);
            sfRenderWindow_display(window);
        } else {
            // In non-graphics mode, just print timing information
            printf("Compute time: %.3f sec (Runs: %d)\n", compute_time, run_count);
            break;
        }
    }

    // Cleanup
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