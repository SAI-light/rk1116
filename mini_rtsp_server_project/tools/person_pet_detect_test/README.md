# person_pet_detect_test v3 — DMA-backed NV12

The prior CPU-memory test reached the detector but failed inside
`rockx_convert_to_i420` because the RV1106 RockIVA build omits the CPU/libyuv
conversion path.

This version keeps NV12 input but allocates the `RockIvaImage` with:

```c
ROCKIVA_IMAGE_AllocMem(&image, ROCKIVA_MEM_TYPE_DMA);
```

The raw NV12 file is copied into the mapped DMA buffer before
`ROCKIVA_PushFrame()`.

## Build

```bash
make clean
make
```

## Run

```bash
LD_LIBRARY_PATH=/oem/usr/lib \
/root/person_pet_detect_test \
  --model-dir /root/rockiva_model \
  --nv12 /root/test_person_896x512.nv12 \
  --width 896 \
  --height 512 \
  --threshold 60
```

Confirm that the startup output shows a non-negative `dataFd`.
