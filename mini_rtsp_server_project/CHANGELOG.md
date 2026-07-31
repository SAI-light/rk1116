# Changelog

## 2.4.0-rc1 — Engineering cleanup

- Add thread-safe ERROR/WARN/INFO/DEBUG logging.
- Add structured command-line options for port, device, recording and log level.
- Add SIGINT/SIGTERM graceful shutdown through a self-pipe.
- Ensure active RTP and MP4 resources are closed from normal thread context.
- Separate fresh-IDR wait time from active media throughput statistics.
- Move all build outputs under `build/<mode>/`.
- Add release/debug Makefile modes, dependency files, strip/install/help targets.
- Update README and validation documentation.
- Add a dry-run-first local history cleanup script.
- Expand `.gitignore` for backups, patch artifacts and generated media.

This release candidate requires final board regression for both TEARDOWN and
Ctrl+C shutdown before tagging as 2.4.0.

## 2.3.0 — Stable live RTSP and MP4 pipeline

- Capture 2304×1296 NV12 frames with V4L2 MMAP.
- Encode H.264 through the RV1106 vendor packet ABI.
- Stream H.264 to VLC over RTP/UDP and RTSP.
- Record the same in-memory H.264 access units through Rockchip RKMuxer.
- Discard stale V4L2 buffers at PLAY and wait for a fresh IDR.
- Verify 363 synchronized frames, zero sequence gaps and a 14.520-second MP4.
