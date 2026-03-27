# libimp-debug

Debug tool for Ingenic IMP (Ingenic Media Platform) on IP camera SoCs.

Communicates with the IMP daemon via POSIX shared memory IPC to query runtime state of the video pipeline, encoder, framesource, audio, and ISP subsystems.

Compatible with all Ingenic SoCs that use the IMP framework: T20, T21, T23, T30, T31, T32, T40, T41, A1.

## Build

Native:
```
make
```

Cross-compile:
```
make CROSS_COMPILE=mipsel-linux-
```

## Usage

```
libimp-debug <command> [args]
```

### Pipeline & System
| Command | Description |
|---------|-------------|
| `--system_info` | Pipeline topology tree with frame counts and semaphore state |
| `--fs_info` | Framesource channel info (resolution, FPS, crop, scaler) |

### Encoder
| Command | Description |
|---------|-------------|
| `--enc_info [chn]` | Encoder channel attributes (codec, RC mode, bitrate, GOP) |
| `--enc_rc_s chn:off:sz:data` | Read/write encoder rate control parameters |

### Audio
| Command | Description |
|---------|-------------|
| `--ai_dev_info` | Audio input device info (sample rate, bit width, gain) |
| `--ai_get_frm <chn>` | Capture audio input frames (on T30: records to PCM file) |
| `--ao_dev_info` | Audio output device info |
| `--ao_get_frm <chn>` | Capture audio output frames |

### ISP (T32+)
| Command | Description |
|---------|-------------|
| `--isp_info` | ISP information |
| `--snap_raw <vinum> [path]` | Capture raw ISP frame |
| `--tuningtool_start <0\|1>` | Start/stop ISP tuning server |

### Misc
| Command | Description |
|---------|-------------|
| `--save_pic [path]` | Save picture to file |
| `--misccmd a:b:c:d` | Send raw misc command |

### Misc command reference (`--misccmd`)

| Command | Description | SoCs |
|---------|-------------|------|
| `100:<chn>:<fmt>:0` | Snap YUV frame (channel 0-2) | All |
| `201:<mode>:0:0` | Set ISP running mode (day/night) | T21, T23, T30 |
| `202:<num>:<den>:0` | Set sensor FPS | T21, T23 |
| `10000:100:0:0` | Dump VBM pool info | T21+ |
| `10000:800:<val>:0` | Set IVS motion dump flag | T20, T21, T23, T31 |

## How it works

The IMP daemon creates a 20KB POSIX shared memory region (`imp_deubg_shm`) and two semaphores (`imp_deubg_sem_tos`, `imp_deubg_sem_toc`). This tool writes a command struct to the shared memory, signals the server semaphore, and waits on the completion semaphore for the response.

The debug handlers are registered inside `libimp.so` during `FrameSourceInit` and `EncoderInit`. On T40/T41, the `mpsys` kernel module must be loaded for handler registration to succeed.

## License

MIT
