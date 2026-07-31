# mini_rtsp_server_project

面向 Luckfox Pico Max（RV1106）的轻量级实时视频服务器。程序在板端完成摄像头采集、H.264 硬件编码、RTSP/RTP 推流和 MP4 同步录像：

```text
/dev/video11（V4L2 MMAP，NV12）
              ↓
      RV1106 MPP/VENC
              ↓
     内存中的 H.264 Access Unit
              ├──→ RTP/H.264 → RTSP → VLC
              └──→ Rockchip RKMuxer → MP4
```

同一个 H.264 Access Unit 只编码一次，并在内存中分发给推流和录像模块；运行过程不生成中间 NV12 或裸 H.264 文件。

## 当前状态

核心链路已经完成并通过板端实测：

| 模块 | 状态 | 验证结果 |
|---|---|---|
| V4L2 摄像头采集 | 已完成 | `/dev/video11`，2304×1296，NV12，多平面 API、单连续平面 |
| MPP H.264 硬编码 | 已完成 | Constrained Baseline，25 fps，GOP 25，目标码率 4 Mbps |
| RTP/H.264 | 已完成 | 单 NALU 与 FU-A 分片，90 kHz 时钟，帧增量 3600 |
| RTSP 控制 | 已完成 | OPTIONS、DESCRIBE、SETUP、PLAY、GET_PARAMETER、TEARDOWN |
| VLC 实时播放 | 已完成 | RTP over UDP 正常出画面 |
| MP4 同步录像 | 已完成 | PLAY 后等待新 IDR，从同一 H.264 AU 实时写入 MP4 |
| 安全退出 | 已实现 | `Ctrl+C`/SIGTERM 唤醒主循环、停止媒体线程并执行 `rkmuxer_deinit()` |
| 日志系统 | 已实现 | ERROR、WARN、INFO、DEBUG 四级运行时日志 |

最终联合验证记录见 [docs/VALIDATION.md](docs/VALIDATION.md)。

## 已知限制

当前版本定位为单路视频验证服务器：

- 仅支持一个 RTSP 客户端；
- RTP 使用 UDP，不支持 RTP over RTSP/TCP；
- 不含音频和 RTCP 统计；
- 视频参数暂固定为 2304×1296、25 fps、GOP 25、4 Mbps；
- 摄像头驱动不接受 `VIDIOC_S_PARM` 时，程序继续使用驱动实际节拍，并以 25 fps 生成 RTP/MP4 时间轴。

## 目录结构

```text
.
├── common/
│   └── log.c                  # 线程安全日志实现
├── include/
│   └── log.h                  # 日志接口与级别
├── capture/
│   ├── v4l2_capture.c
│   └── v4l2_capture.h         # V4L2 MMAP NV12 采集
├── encoder/
│   ├── mpp_encoder.c
│   └── mpp_encoder.h          # RV1106 厂商 Packet ABI 编码路径
├── h264/
│   ├── h264_annexb.c
│   └── h264_annexb.h          # Annex-B NALU 拆分和 SPS/PPS/IDR 检测
├── muxer/
│   ├── mp4_muxer.c
│   └── mp4_muxer.h            # librkmuxer MP4 封装
├── rtp/
│   ├── rtp_packet.c/.h        # RTP Header
│   ├── h264_rtp.c/.h          # 单 NALU 与 FU-A
│   └── rtp_sender.c/.h        # UDP 发送器
├── rtsp/
│   ├── rtsp_request.c/.h
│   ├── rtsp_session.c/.h
│   ├── rtsp_media.c/.h        # 实时媒体线程与 RTP/MP4 双分支
│   └── rtsp_server.c/.h       # RTSP 服务器及安全停机
├── sdp/
│   ├── base64.c/.h
│   └── sdp_builder.c/.h
├── docs/
│   └── VALIDATION.md          # 实测结果
├── scripts/
│   └── cleanup_worktree.sh    # 本地历史/调试文件清理工具
├── main.c                     # 命令行和信号处理
├── Makefile
├── README.md
└── .gitignore
```

## 环境与依赖

### 开发主机

- Ubuntu 24.04；
- Luckfox Pico SDK；
- 交叉编译器：`arm-rockchip830-linux-uclibcgnueabihf-gcc`。

### 开发板

- Luckfox Pico Max / RV1106；
- ARMv7 / uClibc；
- 运行库：

```text
/oem/usr/lib/librockchip_mpp.so.1
/oem/usr/lib/librkmuxer.so
```

### 默认依赖路径

Makefile 默认使用：

```text
MPP_RELEASE=$HOME/boards/rk1116/sdk/luckfox-pico/media/mpp/
            release_mpp_rv1106_arm-rockchip830-linux-uclibcgnueabihf
BOARD_MPP_LIB=$HOME/boards/rk1116/mpp_board_abi
RKMUXER_INCLUDE=$HOME/boards/rk1116/sdk/luckfox-pico/media/out/include
RKMUXER_LIB=$HOME/boards/rk1116/rkmuxer_reference/lib
```

路径不同可以在命令行覆盖。

## 编译

查看配置：

```bash
make print-config
```

编译 Release：

```bash
make clean
make
```

输出文件：

```text
build/release/bin/mini_rtsp_server
```

编译 Debug：

```bash
make BUILD=debug
```

输出文件：

```text
build/debug/bin/mini_rtsp_server
```

覆盖依赖路径示例：

```bash
make \
  MPP_RELEASE=/path/to/mpp/release \
  BOARD_MPP_LIB=/path/to/board/mpp/lib \
  RKMUXER_INCLUDE=/path/to/rkmuxer/include \
  RKMUXER_LIB=/path/to/rkmuxer/lib
```

其他目标：

```bash
make help
make strip
make clean
```

## 命令行

```text
Usage: mini_rtsp_server [options]

  -p, --port PORT         RTSP TCP 端口，默认 8554
  -d, --device PATH       V4L2 节点，默认 /dev/video11
  -o, --record PATH       MP4 路径，默认 /root/live_record.mp4
  -n, --no-record         只推流，不录像
  -l, --log-level LEVEL   error、warn、info 或 debug
  -h, --help              显示帮助
  -V, --version           显示版本
```

默认 INFO 日志只记录生命周期、连接、录像完成和性能汇总。DEBUG 会额外输出 RTSP 请求/回复、SDP、MPP Packet 和周期帧状态。

## 板端运行

先关闭系统自带的 `rkipc`，避免占用摄像头和 RTSP 端口：

```bash
killall rkipc 2>/dev/null
```

传输程序：

```bash
scp build/release/bin/mini_rtsp_server root@172.32.0.93:/root/
```

推流并同步录像：

```bash
cd /root
chmod +x mini_rtsp_server

LD_LIBRARY_PATH=/oem/usr/lib \
./mini_rtsp_server \
  --port 8554 \
  --device /dev/video11 \
  --record /root/live_record.mp4 \
  --log-level info
```

只推流：

```bash
LD_LIBRARY_PATH=/oem/usr/lib \
./mini_rtsp_server --no-record
```

VLC 打开：

```text
rtsp://172.32.0.93:8554/live
```

VLC 需要使用 RTP over UDP，不要强制启用 RTP over RTSP/TCP。

## 安全停止

正常情况下，VLC 点击停止会发送 TEARDOWN，服务器随后关闭 RTP socket 并完成 MP4 索引。

也可以在板端按：

```text
Ctrl+C
```

程序的信号处理器只设置停止标志并写入自管道；实际资源释放在正常线程上下文中完成：

```text
SIGINT/SIGTERM
      ↓
唤醒 poll()/退出 RTSP 循环
      ↓
通知并 join 媒体线程
      ↓
关闭 RTP socket
      ↓
rkmuxer_deinit() 完成 MP4
      ↓
关闭 MPP、V4L2 和监听 socket
```

看到以下日志后再复制 MP4：

```text
[INFO] [muxer] closed: ...
[INFO] [media] live MP4 recording finalized: ...
[INFO] [rtsp] server stopped cleanly
```

不要使用 `kill -9`，因为 SIGKILL 无法执行 MP4 收尾。

## MP4 验证

```bash
ffprobe \
  -v error \
  -count_frames \
  -select_streams v:0 \
  -show_entries \
stream=codec_name,profile,width,height,avg_frame_rate,nb_read_frames:format=format_name,duration,size \
  -of default=noprint_wrappers=1 \
  live_record.mp4
```

完整解码检查：

```bash
ffmpeg -v error -i live_record.mp4 -f null -
```

没有错误输出表示容器和视频帧可以完整解码。

## 仓库规范

仓库应保留：

- 当前正式 `.c/.h`；
- Makefile、README、文档和脚本；
- 必要的小型测试源码。

不应提交：

- 可执行文件、`.o/.d`；
- MP4、H.264、NV12/YUV 和日志；
- SDK、交叉工具链和板端动态库；
- `*_before_*`、`.bak`、失败版源码和补丁中间文件；
- 复制出来的 `stable_*` 快照目录。

稳定状态使用 Git commit/tag 保存。清理本地历史调试文件前先预览：

```bash
./scripts/cleanup_worktree.sh
```

确认后执行：

```bash
./scripts/cleanup_worktree.sh --apply
```

## 后续可扩展方向

- 多客户端会话和独立 RTP 状态；
- RTP over RTSP/TCP；
- RTCP Sender Report；
- 音频采集与 A/V 同步；
- 录像文件轮转和磁盘空间策略；
- V4L2 DMABUF 到 MPP 的零拷贝；
- systemd/启动脚本和健康检查。

## License

仓库尚未指定开源许可证。公开发布前应结合自研代码、Luckfox SDK 和 Rockchip 库的许可要求补充 `LICENSE`。
