# v2.3 fresh PLAY start

This version fixes the one-time V4L2 sequence jump observed immediately after PLAY.

Root cause:
- The camera stream remains active while the RTSP server waits for VLC.
- The four MMAP buffers fill with old frames.
- After PLAY, the application dequeues those four stale frames first.
- The next freshly captured frame carries the current driver sequence, so the
  transition looks like a large sequence gap.

Changes:
1. Drain at most capture.buffer_count queued frames when PLAY starts.
2. Do not send or record the old bootstrap picture.
3. Keep bootstrap SPS/PPS for SDP only.
4. Wait for a fresh post-PLAY IDR, then start RTP and MP4 from that IDR.
5. Establish continuity baseline from the fresh live capture sequence.

Expected logs:
- discarded stale camera buffers at PLAY: count=...
- bootstrap access unit used for SDP only; waiting for a fresh IDR
- fresh live sequence baseline: ...
- first transmitted frame is IDR
- final sequence_gaps=0
