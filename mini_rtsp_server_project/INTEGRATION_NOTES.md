# Live RTSP integration v1

## Scope

This stage replaces the old `test.h264` file source with the verified live
camera and RV1106 MPP encoder pipeline:

```text
/dev/video11 NV12
    -> V4L2 MMAP
    -> RV1106 MPP H264 access unit in memory
    -> Annex-B NAL split
    -> RTP single NAL / FU-A
    -> RTSP over TCP + RTP over UDP
    -> VLC
```

MP4 recording is intentionally not connected in this first RTSP integration
stage. After live VLC playback is verified, the same in-memory H264 access unit
will be sent to both RTP and `mp4_muxer_write_h264()`.

## Main corrections from the file-source version

1. `rtsp_media.c` no longer opens `test.h264`.
2. Runtime SPS/PPS are extracted from the first MPP access unit for SDP.
3. The client IP comes from `accept()`, not the hard-coded `127.0.0.1`.
4. RTP timestamp step is `90000 / 25 = 3600` per encoded frame.
5. One MPP access unit may contain SPS, PPS and IDR NAL units. All NAL units in
   one access unit share one timestamp; only the final RTP packet has marker=1.
6. FU-A applies marker=1 only to the final fragment of the final NAL unit.
7. RTP UDP is bound to the advertised `server_port=5000`.
8. PLAY response is sent before RTP starts.
9. TEARDOWN and GET_PARAMETER are handled.
10. The request-complete helper now returns 0 for incomplete requests.

## First-version constraints

- One RTSP client at a time.
- RTP over UDP only.
- H264 video only; no audio and no RTCP reports yet.
- Fixed camera configuration: 2304x1296 NV12, 25 fps, GOP 25, 4 Mbps.
- RTSP TCP port 8554; RTP/RTCP server ports 5000/5001.

## Build

```bash
cd ~/boards/rk1116/mini_rtsp_server_project
make clean
make
```

Override local paths when required:

```bash
make \
  MPP_RELEASE=/path/to/release_mpp_rv1106_arm-rockchip830-linux-uclibcgnueabihf \
  BOARD_MPP_LIB=/path/to/mpp_board_abi
```

## Board run

```bash
killall rkipc 2>/dev/null
chmod +x mini_rtsp_server
LD_LIBRARY_PATH=/oem/usr/lib ./mini_rtsp_server
```

Expected startup evidence includes:

```text
MPP encoder build: vendor-packet-v9.1-ring-boundary
live bootstrap ready: ... sps=... pps=...
RTSP live media init success: 2304x1296 25fps GOP=25 bitrate=4000000
RTSP live server listening on port 8554
```

Open VLC with:

```text
rtsp://172.32.0.93:8554/live
```

The server ignores the final resource name in this single-stream version, but
`/live` is the documented URL.
