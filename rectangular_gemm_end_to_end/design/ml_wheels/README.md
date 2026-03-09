# Local PyTorch Wheel Cache

Place architecture-specific PyTorch wheels in this directory so `setup_ml_environment.sh` can install them when the board is offline.

## Expected Layout

```
ml_wheels/
  <arch>/
    <python_abi>/
      torch-<ver>-<abi>-<arch>.whl
      torchvision-<ver>-<abi>-<arch>.whl
      torchaudio-<ver>-<abi>-<arch>.whl
```

For the Versal board (AArch64 with CPython 3.10) create:

```
aarch64/
  cp310/
    torch-2.0.1-cp310-cp310-manylinux_2_17_aarch64.manylinux2014_aarch64.whl
    torchvision-0.15.2-cp310-cp310-manylinux_2_17_aarch64.manylinux2014_aarch64.whl
    torchaudio-2.0.2-cp310-cp310-manylinux_2_17_aarch64.manylinux2014_aarch64.whl
```

Copy the actual wheel files into the directory before running `setup_ml_environment.sh --offline` on the board. Wheels can be downloaded from https://download.pytorch.org/whl/torch_stable.html.
