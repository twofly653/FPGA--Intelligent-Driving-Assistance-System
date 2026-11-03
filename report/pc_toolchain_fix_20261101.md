# PC 侧工具链修复说明（2026-11-01 13:10 起）

> 本轮**没有改动固件**，所有改动都在 PC 侧。固件仍是 `vehicle_assist_pcnet_r17_hbfix.elf`
> （1,400,940 B，md5 `3e41e1ebf58a378024dc9a8927cc8ea9`），已在板端 DDR 中运行。

---

## 0 一页速览

| 项 | 内容 |
|---|---|
| 改动文件 | 3 个：`auto_time_sync.py`（新）、`start_live.py`（新）、`ethernet_tracking.py`（改 2 行） |
| 取证工具 | 3 个（新）：`flash_work/box_inject.py`、`flash_work/dump_clock.tcl`、`flash_work/decode_clock.py` |
| 是否动协议 | 否。DVN1 头、CRC、RESULT 布局全部不变 |
| 是否动硬件 | 否 |
| 新增依赖 | `pyserial 3.5`（已装入 `src/pc_vision/.venv`） |
| 解决 | ① 刷固件后时钟失效；② 室内场景永远出不了框（类别写死车辆） |
| 新增能力 | 无人工干预即可验证「叠框链路」的注入器 |

---

## 1 三个问题的定性

### 1.1 `LOCAL TIME` 每次刷写后变空 —— 是"没人下发"，不是固件坏

固件侧早就写对了：

- `ui_clock_poll()` 是**空实现**，时间在读取时从 `g_anchor_ticks` 推算，一次 `TIME SET` 就能永远走秒；
- `ui_app_on_second()` 挂在主循环上（`main.c:237`，`tick_ms>=1000`），首页每秒重绘。

真正的问题是 **PC 侧从来没实现过自动对时**，而 JTAG RAM 重载会把 `g_valid` 打回 0，
所以每次刷完固件首页就停在 `--:--:--` / `WAITING FOR PC SYNC`。

### 1.2 串口号会变，写死端口必然失效

本轮实测：上一轮还在用的 **COM10 已经不存在，板端变成 COM5**。
USB-UART 桥每经历一次插拔/重枚举，编号就会漂移，脚本里写死端口这种设计是不可靠的。

### 1.3 默认类别只有车辆 —— 这是「室内站着也不出框」的真原因

`ethernet_tracking.py` 原来把 `classes=[2,3,5,7]` **写死**在检测调用里，
而 COCO 的 2/3/5/7 是 **car / motorcycle / bus / truck**，不含 person(0)。
也就是说在室内场景下，人站到镜头正前方，检测器也会主动把它过滤掉，
`raw` 永远是 0，屏幕永远没有框。这条限制此前没有任何开关，也没有文档提及。

---

## 2 改动 1：新增 `src/pc_vision/auto_time_sync.py`

**作用**：自动找到板端 UART，把 PC 时钟推过去。

**为什么用握手而不是端口名**：见 §1.2。探测器给每个 COM 口发 `PHOTO STATUS`
（零副作用，`camera_snapshot_command` 只回一行状态），认下面这些前缀的才算板子：

```python
LINE_PREFIXES = ('PHOTO ', 'TEL ', 'STAT ', 'CAMERA ', 'OBSTACLE ',
                 'CMP ', 'TRAFFIC ', 'DETECT ')
```

**行格式必须严格 28 字符**（`ui_clock_command` 的要求）：

```python
line = 'TIME SET ' + when.strftime('%Y-%m-%dT%H:%M:%S')   # 9 + 19 = 28
if len(line) != 28:
    raise SystemExit(...)        # 宁可报错也不发一条会被静默丢弃的行
```

**串口参数**：`115200 8N1`，并且显式关掉 `dsrdtr` / `rtscts`，避免打开端口时误拉控制线。

**用法**：

```bash
python auto_time_sync.py                 # 自动探测端口，立即对时
python auto_time_sync.py --port COM5     # 跳过探测
python auto_time_sync.py --at 2026-11-01T13:20:00
python auto_time_sync.py --dry-run       # 只探测，不发
```

**实测输出**：

```
[time] board UART = COM5 / USB Serial Port (COM5)  (PHOTO IDLE)
[time] sent 28 chars: TIME SET 2026-11-01T13:11:28
[time] board still alive: TEL 1 19 16
```

---

## 3 改动 2：新增 `src/pc_vision/start_live.py`

**作用**：一条命令完成「对时 → 起检测」，顺序不能反（首页只在 `g_valid` 为 1 时才绘制）。

```python
if not known.no_sync:
    auto_time_sync.sync(port=known.port)
cmd = [sys.executable, '-u', os.path.join(HERE, 'ethernet_tracking.py')] + forwarded
```

未识别的参数原样透传，所以既能 `--no-sync`，也能照旧传 `--tracker precision --classes ...`。

**标准启动方式**（替代以前分别跑两个脚本）：

```bash
cd D:/project/FPGA_PYNQ_Integrated/lcd_english_20261028/pc_vision
.venv/Scripts/python.exe -u start_live.py --classes 0 2 3 5 7 --tracker precision
```

---

## 4 改动 3：`ethernet_tracking.py` 增加 `--classes`

只有两行，默认值保持 `[2,3,5,7]`，**现有默认行为完全不变**。

**改前**：

```python
    parser.add_argument('--imgsz',type=int,default=640);parser.add_argument('--show',action='store_true')
...
    model=YOLO(str(args.weights));options=dict(device='0',half=True,imgsz=args.imgsz,conf=.1,iou=.6,classes=[2,3,5,7],verbose=False)
```

**改后**：

```python
    parser.add_argument('--imgsz',type=int,default=640);parser.add_argument('--show',action='store_true')
    # Vehicle classes by default (2=car,3=motorcycle,5=bus,7=truck). Indoor demo
    # needs person(0); pass e.g. --classes 0 2 3 5 7 to widen the filter.
    parser.add_argument('--classes',type=int,nargs='+',default=[2,3,5,7])
...
    model=YOLO(str(args.weights));options=dict(device='0',half=True,imgsz=args.imgsz,conf=.1,iou=.6,classes=args.classes,verbose=False)
```

`nargs='+'` 会在遇到下一个 `--xxx` 时停下，所以 `--classes 0 2 3 5 7 --tracker precision`
能正确解析，不会把 `--tracker` 当成类别。

---

## 5 取证工具：`flash_work/box_inject.py`

**动机**：要证明「叠框链路没坏」，但现场没人时 `count` 必然是 0，无法区分
「链路坏」和「没目标」。注入器绕开这个约束。

**原理**：板端每发出一帧就 `pn_guard_issue(g, frame, capture_us)` 记一张票；
`pn_guard_accept()` 要求 RESULT 的 `(frame, capture_us)` **精确命中某张票**、
`session`/`epoch` 与 `guard` 一致、`capture_us` 在 400 ms 内、`frame` 严格递增、
`width==320 && height==240`。所以只要把**收到的真实帧头原样回填**，框就必然合法。

RESULT 布局（与 `net_protocol.result_packet` 完全一致）：

```
payload = !HH  (proc_ms, count)
       + count × BOX  =  !IHHHHHBB  (id, x1, y1, x2, y2, conf*1000, class_id, 1)
```

**用法**：`python box_inject.py 30`（跑 30 秒）

**实测结果**：

```
[inject] DONE 30.1s frames=164 results=164 rate=5.45 fps  epoch=4
```

板端串口同步出现：

```
OBSTACLE 44 9726 0 1 364 11 1 1 128 96 513 385
STAT ... ob_hw=1 ob_n=1 ob_state=0 ob_proc=11
```

注入的框是 `64,48 → 256,192`，板端报的是 `128,96 → 513,385` ——
说明**板端把 320×240 坐标 ×2 映射到 640×480 显示空间**，这个细节别处没写。

结论：`count=0` 从来不是缺陷，纯粹是画面里没有目标。

---

## 6 真实闭环（不再依赖注入）

```bash
start_live.py --classes 0 2 3 5 7 --tracker precision
```

PC 侧：

```
[live] frame=10759 epoch=8 flags=1 detect=16.6ms raw=1 chosen=1 dropped=0 rewrites=0 session=0xd16eed80
```

板端串口：

```
OBSTACLE 774 10952 0 1 334 13 1 1 4 10 637 479
STAT ... ob_hw=1 ob_n=1 ob_state=0 ob_proc=16
```

**真实检测 → 真实 RESULT → guard 放行 → 硬件表写入**，全线打通。
`raw=1` 检出的是人，框几乎铺满全帧（`4,10 → 637,479`）是因为当时人离镜头太近。

---

## 7 时钟无接触验证法

不想让人去看屏时，用 JTAG 直接读屏显缓存（`dump_clock.tcl` 只读 48 字节，不搬 136 KB 帧缓冲）：

| 符号 | 地址 | 含义 |
|---|---|---|
| `g_anchor_ticks` | `0x0075EA68` | 对时基准 tick |
| `g_valid` | `0x0075EA78` | 是否已成功对时 |
| `g_last_date` | `0x0075EAB4` | 已渲染到面板的日期串 |
| `g_last_clock` | `0x0075EAD4` | 已渲染到面板的时钟串 |

```bash
D:/FPGA/2026.1/Vitis/bin/unwrapped/win64.o/hw_server.exe -p 3121 &
D:/FPGA/2026.1/Vitis/bin/xsdb.bat dump_clock.tcl
python decode_clock.py
```

实测：

```
g_valid        = 1
g_last_date    = 'SUN  2026.11.01'
g_last_clock   = '13:11:50'
pc_clock_now   = '13:11:53'
clock_delta_s  = -3  (IN SYNC)
```

差 3 秒是正常的：`g_last_clock` 是「最后一次渲染」的字符串，不是读取时刻。

---

## 8 坑位清单（本轮新增）

| 坑 | 现象 | 对策 |
|---|---|---|
| 串口号漂移 | 昨天 COM10，今天变 COM5 | 用 `auto_time_sync.py` 握手探测，别写死 |
| 默认只检车辆 | 人站镜头前也 `raw=0` | 加 `--classes 0 2 3 5 7` |
| 坐标空间 | 板端报的框是 PC 的 2 倍 | 板端 320×240 → 显示 640×480，×2 |
| `TIME SET` 无回显 | 发了看不到确认 | 只能靠 `g_valid`/`g_last_clock` 或看屏 |
| `pyserial` 缺失 | venv 里没有 | `python -m pip install pyserial` |

---

## 9 回滚

- 新增的 `auto_time_sync.py` / `start_live.py` / `box_inject.py`：直接删除即可，无副作用。
- `ethernet_tracking.py`：把 `classes=args.classes` 改回 `classes=[2,3,5,7]`，并删掉那两行 `add_argument`，即恢复原状。
