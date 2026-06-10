## 1. SIMD ERA helper implementation

- [ ] 1.1 Add `era_thetas` function pointer field to `struct LW_ACCEL_DISPATCH` in `liblwgeom/lwgeom_accel.h`
- [ ] 1.2 Add scalar fallback `lwgeom_accel_era_thetas_scalar` in `liblwgeom/lwgeom_accel.c` next to the existing scalar dispatch entries; uses `lweci_epoch_to_jd` and `lweci_earth_rotation_angle` from `lwgeom_eci.c`
- [ ] 1.3 Create new file `liblwgeom/accel/era_thetas_neon.c` containing `lwgeom_accel_era_thetas_neon`. Use `float64x2_t` for the fp64 epoch -> JD -> ERA -> mod-2*pi -> direction-sign pipeline, then `vcvt_f32_f64` to narrow each lane to float
- [ ] 1.4 Reuse the existing NEON ERA constants from `rotate_z_neon.c` if accessible; otherwise duplicate them as static `const float64x2_t` in the new file
- [ ] 1.5 Handle the trailing element for odd `npoints` by falling back to the scalar helper for the last point
- [ ] 1.6 Wire the new helpers into `lwaccel_init()` in `lwgeom_accel.c`: NEON variant on `LW_ACCEL_NEON`, scalar fallback otherwise. Skip AVX2 for now (not needed on Apple Silicon target)

## 2. Metal dispatch refactor

- [ ] 2.1 In `liblwgeom/accel/gpu_metal.m`, in `lwgpu_metal_rotate_z_m_epoch()`, replace the inline serial loop that computes thetas with a single call to `lwaccel_get()->era_thetas(data, stride_doubles, n, m_off, dir, thetas)`
- [ ] 2.2 Remove the now-unused inline helpers `metal_epoch_to_jd` and `metal_earth_rotation_angle` from `gpu_metal.m`
- [ ] 2.3 Verify the dispatch table is initialized before this call site; the existing rotate_z_m_epoch_neon path depends on the same initialization order so it should be safe

## 3. Tests

- [ ] 3.1 Add `test_simd_era_thetas_scalar` in `liblwgeom/cunit/cu_metal.c` (or a new `cu_simd_era.c`) that calls the scalar helper directly with a known input and asserts each output element equals the expected float-narrowed scalar reference value
- [ ] 3.2 Add `test_simd_era_thetas_neon` that does the same for the NEON variant, comparing element-by-element to the scalar variant (max diff SHALL be 0.0 because both narrow the same fp64 value)
- [ ] 3.3 Add `test_metal_rotate_z_m_epoch_simd_era` that runs `lwgpu_metal_rotate_z_m_epoch` with the SIMD helper in the dispatch table and asserts the output matches the scalar reference within `FP32_EARTH_SCALE_TOLERANCE`
- [ ] 3.4 Update the `metal_suite_setup()` call to register the new tests

## 4. Benchmark instrumentation

- [ ] 4.1 In `liblwgeom/bench/bench_metal.c`, add a new sub-benchmark or extend the existing rotate_z_m_epoch case to time the host ERA precompute and the GPU dispatch+execute as separate phases
- [ ] 4.2 Use `now_us()` (already in the file) to capture timestamps before/after the `era_thetas` call and before/after the GPU dispatch -- may need a hook in `gpu_metal.m` to expose the phase boundary
- [ ] 4.3 Add CSV columns `host_era_us` and `gpu_kernel_us` to the benchmark output
- [ ] 4.4 Run on Apple A18 Pro at 100K, 500K, 1M points and capture results for the design.md update

## 5. Verification

- [ ] 5.1 Run `./cu_tester metal` and confirm all tests pass (existing 6 + new 3 = 9 tests)
- [ ] 5.2 Run `./bench_metal` and confirm `rotate_z_m/metal` at 500K points achieves ≥114 Mpts/s (1.5x current NEON 76 Mpts/s)
- [ ] 5.3 Run `./bench_metal --csv` and confirm the new `host_era_us` and `gpu_kernel_us` columns are populated
- [ ] 5.4 Verify no regression in `rotate_z_m/scalar` and `rotate_z_m/neon` standalone benchmarks

## 6. Documentation

- [ ] 6.1 Update `apple-metal-gpu-backend/design.md` Risks/Trade-offs section: mark the "Host-side ERA precomputation serializes the hot path" entry as resolved with a one-line back-reference to this change and the new measured throughput
- [ ] 6.2 Update `lwgeom_accel.c`'s `effective_gpu_threshold()` comment with the new measured Metal performance, but do NOT change the 5x multiplier in this commit
- [ ] 6.3 Add a brief note in `liblwgeom/accel/gpu_metal.m`'s file header about the new dispatch path through `lwaccel_get()->era_thetas`

## 7. Merge and archive

- [ ] 7.1 Open PR from `metal-simd-era-precompute` implementation branch to fork develop
- [ ] 7.2 Wait for CI green
- [ ] 7.3 Merge to develop
- [ ] 7.4 Archive this openspec change via `openspec archive metal-simd-era-precompute`
