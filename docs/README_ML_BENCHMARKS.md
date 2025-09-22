# ML Benchmarks Configuration

## ENABLE_ML_BENCHMARKS Parameter

The `ENABLE_ML_BENCHMARKS` parameter in `config.json` controls whether PyTorch and NumPy benchmarks are run during application execution.

### Values:
- **0 (default)**: ML benchmarks are disabled. The application will prompt you at runtime if you want to run them.
- **1**: ML benchmarks are automatically enabled and will run without prompting.

### Behavior:

#### When ENABLE_ML_BENCHMARKS = 0 (default):
```
=== ML BENCHMARK COMPARISON ===
ML benchmarks are disabled in config.json (ENABLE_ML_BENCHMARKS=0)
Would you like to run PyTorch and NumPy benchmarks? (y/N): 
```
- If you answer 'y': Installs ML environment and runs benchmarks
- If you answer 'n' or press Enter: Skips ML benchmarks

#### When ENABLE_ML_BENCHMARKS = 1:
```
=== ML BENCHMARK COMPARISON (ENABLED) ===
AI Engine Target: hw_emu
ML benchmarks are enabled in config.json - installing ML environment...
✓ ML environment setup completed successfully!
Running NumPy CPU benchmark...
Running PyTorch CPU benchmark...
```
- **Automatically installs** ML environment on target board
- **Automatically runs** ML benchmarks without prompting
- **No user interaction** required

### Installation:
- ML environment is installed using `setup_ml_environment.sh`
- Installs: NumPy, PyTorch, scipy, matplotlib, pandas
- Compatible versions for aarch64 architecture

### Performance Impact:
- **Installation time**: ~2-5 minutes (one-time setup)
- **Runtime overhead**: ~10-30 seconds for benchmarks
- **Storage**: ~500MB for ML packages

### Recommendation:
- Set to **0** for development/testing (saves time)
- Set to **1** for production/benchmarking (automatic execution)
