# Validation Record

本文件记录 `mini_rtsp_server_project` 已完成的板端验证，避免将大体积日志、H.264 或 MP4 样本提交到 Git 仓库。

## 1. 摄像头到 MP4 独立链路

配置：

```text
设备：/dev/video11
分辨率：2304×1296
像素格式：NV12
编码：H.264 Constrained Baseline
帧率：25 fps
GOP：25
目标码率：4 Mbps
```

60 秒结果：

```text
encoded frames      : 1500/1500
key frames          : 60
sequence gaps       : 0
wall-clock time     : 60.006 s
end-to-end speed    : 25.00 fps
```

`ffprobe`：

```text
codec_name=h264
profile=Constrained Baseline
width=2304
height=1296
avg_frame_rate=25/1
nb_read_frames=1500
duration=60.000000
```

`ffmpeg -v error -i ... -f null -` 无错误输出。

## 2. 摄像头到 RTSP/VLC

已验证：

- RTSP TCP 8554 正常监听；
- RTP UDP 5000 正常绑定；
- OPTIONS、DESCRIBE、SETUP、PLAY、TEARDOWN 正常；
- SDP 使用实时 MPP 首帧提取的 SPS/PPS；
- VLC 3.0.23 正常实时出画面；
- TEARDOWN 后媒体线程和 RTP socket 正常退出。

## 3. RTSP 推流与 MP4 同步录像

v2.3 联合测试：

```text
processed frames      : 363
recorded frames       : 363
live captured frames  : 363
sequence gaps         : 0
key frames            : 15
MP4 timeline duration : 14.520 s
source sequence rate  : 25.00 sequence/s
```

生成文件：

```text
codec_name=h264
profile=Constrained Baseline
width=2304
height=1296
avg_frame_rate=25/1
nb_read_frames=363
duration=14.520000
size=6837611
```

完整 FFmpeg 解码无错误。

## 4. 启动时新鲜 IDR 策略

服务器初始化时编码的 bootstrap AU 只用于 SDP 的 SPS/PPS。PLAY 后：

1. 丢弃 V4L2 队列中陈旧缓冲帧；
2. 等待实时编码器产生新的 IDR；
3. 从该 IDR 同时启动 RTP 和 MP4；
4. 非 IDR 等待帧不计入正式录像时间轴。

该策略避免客户端等待连接期间的旧图像被作为直播首帧或 MP4 首帧。

## 5. v2.4 工程收尾待验证项

v2.4 新增日志、命令行和 SIGINT/SIGTERM 安全退出。合并前建议完成两项回归：

### 正常 TEARDOWN

- VLC 正常出画面；
- VLC 点击停止；
- MP4 finalized；
- `ffprobe` 帧数、时长正确；
- FFmpeg 完整解码无错误。

### Ctrl+C

- VLC 正在播放且 MP4 正在增长时按 Ctrl+C；
- 程序输出 muxer closed、recording finalized、server stopped cleanly；
- 进程退出码为 0；
- 生成的 MP4 可被 ffprobe/ffmpeg 完整读取。
