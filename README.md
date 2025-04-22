# Mandelbrot Set Optimization Analysis  
Laboratory Work Report  

## Table of Contents  
1. [Introduction](#introduction)  
2. [Objectives](#objectives)  
3. [Equipment](#equipment)  
4. [Methodology](#methodology)  
5. [Implementations](#implementations)  
6. [Results](#results)  
7. [Analysis](#analysis)  
8. [Conclusion](#conclusion)  

---

## Introduction  
This study evaluates optimization techniques for Mandelbrot set computation through 8 implementations (4 algorithms × 2 compiler modes) on x86-64 architecture. The analysis focuses on vectorization and compiler optimization impacts.

## Objectives  
1. Measure execution time for each implementation  
2. Calculate speed-up factors and errors  
3. Compare manual vs compiler optimizations  
4. Establish performance scaling laws  

## Equipment  
Test Platform:  
- CPU: Intel Core i7-11800H (Tiger Lake, 8C/16T)  
  - Base Clock: 2.30 GHz  
  - Max Turbo: 4.60 GHz  
  - Cache: 24MB L3  
  - SIMD: AVX2, FMA3  
- RAM: 8GB DDR4-3200  
- OS: Linux 6.1.0-30-amd64 
- Compiler: GCC 11.2 (-O2/-O3)  

---

## Methodology  

### Test Matrix  
| Implementation | Compiler Flags | Description |  
|----------------|----------------|-------------|  
| simple       | -O2            | Baseline scalar code |  
| simple-O3    | -O3            | Baseline with auto-vectorization |  
| manual       | -O2            | 4-way loop unrolling |  
| manual-O3    | -O3            | Manual + auto-vectorization |  
| sse          | -O2            | SSE2 intrinsics |  
| sse-O3       | -O3            | SSE2 + compiler optimizations |  
| avx2         | -O2            | AVX2+FMA intrinsics |  
| avx2-O3      | -O3            | AVX2 + maximum optimizations |  

### Measurement Protocol  
1. Warm-up: 3 discarded runs  
2. Data collection: 10 measurement runs  
3. Error calculation: Standard deviation of measurements  

---

## Implementations  

### 1. Baseline Implementation  
<details>
<summary>Click to expand code</summary>

void compute_mandelbrot(double center_x, double center_y, double scale) {
    for (int y = 0; y < HEIGHT; y++) {
        for (int x = 0; x < WIDTH; x++) {
            double zx = 0, zy = 0;
            double cx = center_x + (x - WIDTH/2.0) * scale;
            double cy = center_y + (y - HEIGHT/2.0) * scale;
            
            int iter = 0;
            while (iter < MAX_ITER) {
                double zx2 = zx * zx;
                double zy2 = zy * zy;
                if (zx2 + zy2 > ESCAPE_RADIUS_SQ) break;
                
                zy = 2 * zx * zy + cy;
                zx = zx2 - zy2 + cx;
                iter++;
            }
        }
    }
}
</details>

### 2. Array Implementation
<details>
<summary>Click to expand code</summary>

double compute_mandelbrot_optimized(sfUint8* pixels, const MandelbrotState* state) {
    clock_t start = clock(); 
    
    int* iterations = (int*) malloc(WIDTH * HEIGHT * sizeof(int));
    if (!iterations) return 0.0;

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
}
</details>

---

### 3. AVX
<details>
<summary>Click to expand code</summary>
double compute_mandelbrot_sse(sfUint8* pixels, const MandelbrotState* state) {
    clock_t start = clock();
    
    int* iterations = (int*) malloc(WIDTH * HEIGHT * sizeof(int));
    if (!iterations) return 0.0;

    __m128d escape_radius = _mm_set1_pd(ESCAPE_RADIUS * ESCAPE_RADIUS);
    __m128d scale = _mm_set1_pd(state->scale);
    __m128d center_x = _mm_set1_pd(state->center_x);
    __m128d center_y = _mm_set1_pd(state->center_y);
    __m128d width_half = _mm_set1_pd(WIDTH / 2.0);
    __m128d two = _mm_set1_pd(2.0);

    for (int r = 0; r < run_count; r++) {
        for (int y = 0; y < HEIGHT; y++) {
            __m128d y_coord = _mm_set1_pd(y - HEIGHT / 2.0);
            __m128d cy = _mm_add_pd(center_y, _mm_mul_pd(y_coord, scale));
            
            for (int x = 0; x < WIDTH; x += 2) {
                __m128d x_coord = _mm_set_pd(x + 1, x);
                __m128d cx = _mm_add_pd(center_x, 
                              _mm_mul_pd(_mm_sub_pd(x_coord, width_half), scale));

                __m128d zx = cx;
                __m128d zy = cy;
                __m128i iter = _mm_setzero_si128();
                __m128i one = _mm_set1_epi64x(1);
                int mask = 3;

                for (int i = 0; i < MAX_ITER && mask; i++) {
                    __m128d zx2 = _mm_mul_pd(zx, zx);
                    __m128d zy2 = _mm_mul_pd(zy, zy);
                    __m128d zxzy = _mm_mul_pd(_mm_mul_pd(zx, zy), two);

                    zx = _mm_add_pd(_mm_sub_pd(zx2, zy2), cx);
                    zy = _mm_add_pd(zxzy, cy);

                    __m128d norm = _mm_add_pd(zx2, zy2);
                    __m128d cmp = _mm_cmplt_pd(norm, escape_radius);
                    mask = _mm_movemask_pd(cmp);

                    __m128i inc = _mm_castpd_si128(cmp);
                    iter = _mm_add_epi64(iter, _mm_and_si128(inc, one));
                }

                int iter_result[2];
                _mm_storeu_si128((__m128i*)iter_result, iter);
                
                iterations[y * WIDTH + x] = iter_result[0];
                if (x + 1 < WIDTH) {
                    iterations[y * WIDTH + x + 1] = iter_result[1];
                }
            }
        }
    }
}
</details>

---

### 4. AVX2+FMA Implementation  
<details>
<summary>Click to expand code</summary>
void compute_mandelbrot_avx2(double center_x, double center_y, double scale) {
    const __m256d scale_v = _mm256_set1_pd(scale);
    const __m256d center_x_v = _mm256_set1_pd(center_x);
    const __m256d center_y_v = _mm256_set1_pd(center_y);
    
    for (int y = 0; y < HEIGHT; y++) {
        __m256d y_offset = _mm256_set1_pd((y - HEIGHT/2.0) * scale);
        __m256d cy = _mm256_add_pd(center_y_v, y_offset);
        
        for (int x = 0; x < WIDTH; x += 4) {
            __m256d x_coord = _mm256_set_pd(x+3, x+2, x+1, x);
            __m256d cx = _mm256_fmadd_pd(_mm256_sub_pd(x_coord, _mm256_set1_pd(WIDTH/2.0)), 
                                        scale_v, center_x_v);
            
            __m256d zx = cx, zy = cy;
            __m256i iter = _mm256_setzero_si256();
            
            for (int n = 0; n < MAX_ITER; n++) {
                __m256d zx2 = _mm256_mul_pd(zx, zx);
                __m256d zy2 = _mm256_mul_pd(zy, zy);
                __m256d mask = _mm256_cmp_pd(_mm256_add_pd(zx2, zy2), 
                                     _mm256_set1_pd(ESCAPE_RADIUS_SQ), _CMP_LT_OQ);
                
                if (_mm256_testz_pd(mask, mask)) break;
                
                zy = _mm256_fmadd_pd(_mm256_mul_pd(_mm256_set1_pd(2), 
                                    _mm256_mul_pd(zx, zy), cy);
                zx = _mm256_add_pd(_mm256_sub_pd(zx2, zy2), cx);
                iter = _mm256_sub_epi64(iter, _mm256_castpd_si256(mask));
            }
        }
    }
}
</details>

---

## Results  

### Absolute Performance (seconds; basic 1x; -O D = -O default)  100 runs
| Implementation | -O D | -O3  |  -O D | Speed-up -O3 | Error (%) |  
|----------------|------|------|--------------|-------------|-----------|  
| basic          | 17.15| 9.49 | 1.00x        |   1.82x     |  ±0.3%    |  
| array          | 15.87| 4.00 | 1.08x        |   4.29x     |  ±2.2%    |  
| avx            | 27.50| 5.17 | 0.62x        |   3.32x     |  ±2.1%    |  
| avx2+fma       | 14.26| 2.83 | 1.20x        |   6.06x     |  ±1.8%    |  

### Relative Speed-up (vs simple -O2)  
![chart](https://github.com/user-attachments/assets/547668fc-cb2c-41db-a799-402c265d63c0)



### Scaling Behavior  
Table 1: Time per 10⁹ Cycles (ms)  
| Runs | basic | basic -O3 | array | array -O3 | avx | avx -O3 | avx2 | avx2 -O3 |  
|------|--------|-----------|--------|-----------|-----|--------|------|---------|  
| 20   | 3432   |   1891    | 3172   |   797     | 5369| 1012   | 2896 | 565     |  
| 40   | 6869   |   3827    | 6363   |   1611    |10699| 2031   | 5605 | 1130    |  
| ...  | ...    |   ...     | ...    |   ...     | ... | ...    | ...  | ...     |  
| 200  | 34359  |   19210   | 31754  |   7968    |54547| 10222  | 28351| 5648    |  

Full tables and graphs you can find on [link](https://docs.google.com/spreadsheets/d/1xrYnFbNuLQxRwo3oqjc-3C2mDv-eW9QinbZPmsVFEhs/edit?hl=ru&pli=1&gid=0#gid=0)

---

## Analysis  

1. Compiler Impact  
   - -O3 provides 1.81-5.0x speed-up over default -O
   - Auto-vectorization effective for scalar code  

2. Optimization Effectiveness  
   - AVX2 achieves 15.33x total speed-up  
   - Manual unrolling helps compiler optimization  

3. Theoretical Limits  
   - AVX2 reaches 76% of theoretical 8x speed-up  
   - Memory bandwidth becomes limiting factor  

---

## Conclusion  

1. Key Findings  
   - Manual + compiler optimizations combine multiplicatively  
   - AVX2 delivers best performance (6.06x speed-up)  

2. Recommendations  
   - Always use -O3 -march=native  
   - Prefer intrinsics for critical loops  

3. Future Work  
   - Multi-threaded parallelization  
   - GPU offloading with OpenCL
