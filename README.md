# MP4 阅读器与媒体中心

基于 [Waveshare ESP32-P4-WIFI6-Touch-LCD-4.3](https://docs.waveshare.net/ESP32-P4-WIFI6-Touch-LCD-4.3) 的
掌上媒体终端固件：480 × 800 MIPI-DSI 触摸屏 + ESP32-P4 + 32 MB PSRAM + Micro SD + ESP32-C6（WiFi 6 / BLE）。

- 开发框架：ESP-IDF **v5.5.4**（工程 `.vscode/settings.json` 已指向该版本）
- 图形库：LVGL 9.5（通过官方 BSP `waveshare/esp32_p4_wifi6_touch_lcd_4_3 == 1.0.1` 适配屏幕与触摸）
- 首页为应用宫格：阅读 / 图片 / 音乐 / 视频 / 蓝牙 / 时钟 / 设置

## 功能

### 桌面与时钟

- 状态栏：时间、日期、WiFi 状态、SD 剩余空间，每秒刷新
- 时钟：大号时间 + 日期 + 星期，支持手动触发 NTP 校时（`ntp.aliyun.com`，CST-8）
- 未联网时显示“未校时”，联网后自动校时并持续走时

### 阅读

- 书架：扫描 SD 卡 `/sdcard/books`（含一级子目录），显示书名、格式、大小、已读进度，长按删除
- 阅读：TXT（UTF-8 / UTF-16 / GBK 自动识别）与 EPUB（zip + OPF/NCX 解析，正文转纯文本）
- 排版：按中英文混排规则自动断行、分页；支持 22 / 26 / 30 px 三档字号与行距
- 交互：左右点击区域 / 左右滑动翻页，中间点击呼出菜单
- 菜单：目录（章节目录 + 书签列表）、字号加减、主题切换（纸白 / 夜间 / 护眼）、添加书签、返回书架

### 图形化联网（WiFi 2.4G）

- 「设置 → WiFi」或首页「蓝牙/设置」进入：扫描附近热点，显示 SSID、信号强度、是否加密
- 点击热点：开放网络直接连接，加密网络弹出屏幕键盘输入密码
- 显示当前连接状态与 IP，可一键断开并清除配置
- 也支持 SD 卡根目录 `wifi.txt` 作为首次配置
- 连接后自动 SNTP 校时，并启动网页书库上传服务

### 蓝牙（BLE）

- 板载 ESP32-C6 通过 esp_hosted（VHCI over SDIO）提供 BLE，P4 侧运行 NimBLE 主机
- 中心模式：扫描附近 BLE 设备（名称 / 地址 / RSSI），点击连接
- 从机模式：设备以 `MP4-Reader` 广播，手机用「BLE 调试助手」连接后，
  向 RX 特征（`6E400002…`）写文本，设备回显到 TX 特征（`6E400003…`），并显示在蓝牙页
- **仅支持 BLE**：P4 + C6 方案不支持经典蓝牙 / A2DP 蓝牙耳机

### 图片

- 扫描 `/sdcard/pictures`，支持 `.jpg` `.jpeg` `.png` `.bmp` `.qoi`
- 使用 P4 硬件 JPEG 解码（`esp_lv_decoder`），PNG / QOI 走软件解码
- 全屏查看：左右滑动或「上一张 / 下一张」切换，「放大 / 缩小 / 复位」（0.5× ~ 3×）

### 音频

- 扫描 `/sdcard/music`，支持 `.mp3` `.wav`
- 播放 / 暂停 / 继续 / 停止、上一首 / 下一首、音量滑条
- ES8311 + 板载功放输出，MP3 由 libhelix 解码

### 视频

- 扫描 `/sdcard/videos`，支持 `.mp4` `.avi`
- 容器解析复用官方 `esp_extractor`，视频需为 **MJPEG** 编码，音轨支持 AAC / MP3
- 画面经硬件 JPEG 解码后作为 LVGL 图像刷新到屏幕

### 设置

- 屏幕亮度、正文字号、阅读主题
- WiFi / 蓝牙 / 时钟入口
- 网页上传地址、SD 卡剩余空间、版本信息

### 网页上传

浏览器打开 `http://<设备IP>/` 可上传/删除书籍、修改 WiFi 账号密码。

## 目录结构

```
main/
├── main.c                 # 入口：BSP 屏幕初始化 → app_start()
├── app/
│   ├── app.c              # NVS / 设置 / SD / WiFi / 字体 / 图片解码器 / 首屏
│   ├── clock.c            # 本地时间、日期格式化、校时状态
│   ├── fonts.c            # 解析内置 .bin 中文字体（3 档字号，多包 fallback 串联）
│   ├── fonts_manifest.h   # 自动生成：字体包清单
│   └── screenshot.c       # 帧缓冲经串口导出（调试用）
├── book/                  # 书籍解析（TXT / EPUB / zip / GBK 表）
├── layout/layout.c        # 断行 + 分页 + 进度定位
├── storage/storage.c      # SD 挂载、书库/媒体扫描、NVS 设置/进度/书签
├── audio/player.c         # ES8311 + chmorgan/esp-audio-player（MP3/WAV）
├── video/
│   ├── app_extractor.c    # 官方 10_mp4_player：容器/音轨抽帧
│   └── app_stream_adapter.c # JPEG 硬件解码 + 帧回调
├── ui/                    # LVGL 界面
│   ├── home.c             # 桌面宫格 + 状态栏
│   ├── library.c          # 书架
│   ├── reader.c           # 阅读页
│   ├── toc.c              # 目录 / 书签
│   ├── gallery.c          # 图片列表 + 全屏查看
│   ├── music_screen.c     # 音乐列表 + 播放控制
│   ├── video.c            # 视频列表 + 播放页
│   ├── wifi_screen.c      # WiFi 扫描 / 密码键盘 / 连接状态
│   ├── ble_screen.c       # BLE 扫描 / 连接 / 手机收发文本
│   ├── clock_screen.c     # 时钟页
│   ├── settings_screen.c  # 设置页
│   ├── theme.c / widgets.c
├── net/
│   ├── wifi.c             # STA 连接、扫描、SNTP、断开
│   ├── ble.c              # NimBLE 中心 + 外设（NUS）
│   └── web_server.c       # HTTP 服务：上传 / 删除 / WiFi 配置
fonts/                     # 生成的 cjk_{字号}_{包号}.bin，构建时嵌入应用固件
components/esp_extractor/  # 官方预编译容器解析库（MP4/AVI/WAV…）
tools/gen_fonts.py         # 生成并拆包中文字体（需要 Node + lv_font_conv + fonttools）
tools/gen_gbk_table.py     # 生成 GBK 映射表
```

## 编译与烧录

```powershell
# 1. 激活 IDF 环境（按本机安装路径）
& C:\Espressif\frameworks\esp-idf-v5.5.4\export.ps1

# 2. 首次编译（target 已在 sdkconfig.defaults 中指定为 esp32p4）
idf.py build

# 3. 烧录并查看日志
idf.py -p COMx flash monitor
```

首次编译会由组件管理器自动下载依赖（BSP、LVGL、esp_hosted、esp_audio_codec、esp-audio-player 等）。
`fonts/cjk_*.bin` 通过 CMake 的 `EMBED_FILES` 直接嵌入应用固件（Flash 映射、零拷贝），
因此启动时不需要从文件系统读取字体，开机即可显示界面。

> 固件约 **9.7 MB**，按 460800 波特率烧录约需 3~4 分钟；`idf.py flash` 会同时写入
> 分区表（factory 已扩到 16 MB），换过分区表的板子必须整片烧录一次。

分区表（`partitions.csv`）：

| 名称 | 类型 | 偏移 | 大小 |
| --- | --- | --- | --- |
| nvs | data/nvs | 0x9000 | 24 KB |
| phy_init | data/phy | 0xF000 | 4 KB |
| factory | app | 0x10000 | 16 MB（含 ~7 MB 字体） |

## 字体

LVGL 自带字体不含中文，因此仓库中预置了用 `lv_font_conv` 生成的中文二进制字体，
构建时嵌入固件（`main/CMakeLists.txt` 用 `GLOB` 收集 `fonts/cjk_*.bin`），运行时用
`lv_binfont_create_from_buffer()` 直接解析，启动无需文件系统：

- 字符集：GB2312 全部 **6763 个汉字**（一级 3755 + 二级 3008）+ 符号区 + ASCII + 中文标点（共 7445 字符）
- 字号：22 / 26 / 30 px（4 bpp，共约 7.1 MB；22/26 开机加载，30 首次使用时解析）
- **每个字号按字形位图大小拆成多个包**（`cjk_22_0.bin`、`cjk_22_1.bin` …），
  运行时用 `lv_font_t.fallback` 串成链
- 原先的 34 px 档位已移除，为视频解码库腾出约 3.9 MB Flash

### 为什么必须拆包 + 非压缩

这两点是踩过坑之后的硬约束，改动字体生成参数前务必先读：

1. **必须 `--no-compress`。**
   `lv_font_conv` 的 RLE（带 prefilter）压缩位图在 LVGL 9.5 上解出来的字形是乱的，
   表现为整段中文糊成一团；非压缩（PLAIN）路径正常。

2. **单个字体包解码后的位图必须 < 1 MB。**
   LVGL 的 `lv_font_fmt_txt_glyph_dsc_t.bitmap_index` 只有 **20 位**，
   上限 1,048,575 字节。中文字库 4 bpp 时单个字号约 1.6~3.9 MB，
   索引回绕后**后半段字形会指到错误位置**，表现为部分汉字变成乱码
   （典型现象：`书库刷新` 正常、`设置请` 变形状）。
   `tools/gen_fonts.py` 会按 800 KB/包自动切分，并在生成后校验每包位图字节数。

重新生成（需要 Node.js 与 Python）：

```powershell
pip install fonttools
python tools/gen_fonts.py --sizes 22 26 30   # 自动下载 Noto Sans SC 并裁剪、拆包
```

脚本会同时重写 `main/app/fonts_manifest.h`（包清单），`fonts.c` 依据它加载。

> 说明：字体覆盖 GB2312 全部汉字，GBK 扩展区的生僻字仍会缺字（显示为方块）。

## SD 卡准备

```
SD 卡根目录
├── books/                 # 电子书（支持子目录）
├── pictures/              # 图片（jpg / png / bmp / qoi）
├── music/                 # 音乐（mp3 / wav）
├── videos/                # 视频（mp4 / avi，MJPEG）
└── wifi.txt               # 可选，首次配置 WiFi
```

`wifi.txt` 内容：

```
ssid=你的WiFi名称
pass=你的WiFi密码
```

连接成功后也可以在设备「设置 → WiFi」中重新扫描、输入密码，或直接访问 `http://<IP>/`。

## 操作说明

| 操作 | 效果 |
| --- | --- |
| 首页点击应用卡片 | 进入阅读 / 图片 / 音乐 / 视频 / 蓝牙 / 时钟 / 设置 |
| 点击屏幕左 1/4 | 阅读：上一页（章首则跳到上一章末页） |
| 点击屏幕右 1/4 | 阅读：下一页（章末则进入下一章） |
| 左右滑动 | 阅读翻页；图片查看切换上一张 / 下一张 |
| 点击屏幕中间 | 阅读：呼出/收起菜单 |
| 长按书架条目 | 删除书籍 |
| 菜单「目录」 | 章节目录 / 书签列表，点击跳转 |
| 菜单「字号 -/+」 | 切换字号并保持当前阅读位置 |
| 菜单「主题」 | 纸白 → 夜间 → 护眼 |
| 菜单「书签」 | 在当前位置添加书签 |
| WiFi 页点击热点 | 输入密码并连接（开放网络直接连接） |
| 蓝牙页点击设备 | 连接该 BLE 设备 |

## HTTP 接口

| 方法 | 路径 | 说明 |
| --- | --- | --- |
| GET | `/` | 网页管理界面 |
| GET | `/api/books` | 书库 JSON |
| GET | `/api/status` | WiFi / 存储状态 JSON |
| POST | `/upload` | multipart 表单上传书籍（≤16 MB） |
| POST | `/api/delete` | 表单参数 `path`，删除 SD 卡中的书 |
| POST | `/api/wifi` | 表单参数 `ssid` / `pass`，保存并重连 |

## 开发说明

### 数据流

```
SD 卡文件
  └─ book.c            按扩展名分发
       ├─ book_txt.c   编码识别（UTF-8/UTF-16/GBK）→ 章节切分
       └─ book_epub.c  zip 解包 → container.xml → OPF → NCX 目录 → HTML 转纯文本
            └─ layout.c        按中英混排规则断行 → 按屏幕尺寸分页
                 └─ ui/reader.c 渲染当前页、翻页、进度保存
```

`layout.c` 只依赖「字号 + 行距 + 可用高度」，不持有 LVGL 对象，
因此切换字号后只需重新分页、按字符偏移恢复原阅读位置。

媒体播放：

```
/sdcard/music/a.mp3 → audio/player.c → chmorgan/esp-audio-player → ES8311
/sdcard/pictures/*.jpg → LVGL FS(S:) → esp_lv_decoder → 屏幕
/sdcard/videos/*.mp4 → video/app_extractor.c → JPEG 硬解 → LVGL 图像控件
                                      └→ AAC/MP3 解码 → ES8311
```

### 存储布局

| 位置 | 用途 |
| --- | --- |
| NVS 命名空间 `reader`，键 `cfg` | 全局设置（字号、主题、亮度、上次阅读的书与位置） |
| NVS 命名空间 `prog`，键为书名 hash | 每本书的阅读进度 |
| NVS 命名空间 `mark`，键为书名 hash | 每本书的书签列表 |
| NVS 命名空间 `wifi` | WiFi SSID / 密码 |
| SD `/sdcard/books` | 书籍文件（支持一级子目录） |
| SD `/sdcard/pictures`、`/music`、`/videos` | 图片 / 音乐 / 视频 |
| SD `/wifi.txt` | 可选的首次 WiFi 配置 |

### 资源占用

- 应用固件 **9.7 MB**，分区 16 MB（余量约 39%）；其中中文字体 **7.10 MB / 12 个包**
- 字体按字号懒加载：22/26 px 开机加载，30 px 首次切换时解析
- DPI 帧缓冲 3 × 750 KB 在 PSRAM；LVGL 绘制缓冲 480×50×2 = 48 KB 在内部 RAM
- LVGL 图片缓存 1.5 MB（PSRAM），用于图片查看
- 视频播放时占用 1 个 750 KB 帧缓冲 + 256 KB JPEG 输入缓冲（PSRAM）
- 打开大 TXT（>10 MB）时整本读入 PSRAM，注意与字体、视频共存

### 关键约束（改动前务必确认）

1. **芯片 revision**：板载为 ESP32-P4 **Rev1.3**，`sdkconfig.defaults` 需对应配置（见排障）
2. **显示刷新模式**：必须用 `ESP_LV_ADAPTER_TEAR_AVOID_MODE_NONE`（见排障）
3. **字体生成参数**：必须 `--no-compress` 且单包位图 < 1 MB（见“为什么必须拆包 + 非压缩”）
4. **依赖版本**：`esp_hosted` / `esp_wifi_remote` 固定在 1.4.7 / 0.14.5（见下文）
5. **界面布局**：不要按「字号 px」估算控件高度。中文字体 22 px 时 `line_height` 实际是 **43 px**
   （ascent 30 / descent -13），卡片和容器一律用 `LV_SIZE_CONTENT` + flex 自适应
6. **BLE 必须走 esp_hosted**：`CONFIG_BT_NIMBLE_TRANSPORT_UART` 必须关闭，
   `CONFIG_ESP_HOSTED_ENABLE_BT_NIMBLE=y`，否则 NimBLE 会去找 UART HCI
7. **图片路径**：LVGL 用 `S:` 盘符映射 `/sdcard`（`CONFIG_LV_USE_FS_POSIX`），
   代码里是 `S:/pictures/a.jpg`，不是 `/sdcard/pictures/a.jpg`

## 已知限制

- 视频只支持 **MJPEG 编码**的 MP4/AVI，H.264 不支持；为省内存使用单帧缓冲，快速运动画面可能有撕裂
- 视频解码输出是字节序相反的 RGB565，显示时用 `LV_COLOR_FORMAT_RGB565_SWAPPED` 直接呈现，不做逐帧转换
- 音频与 BLE 共用内部 DMA 内存：进入蓝牙页会先释放音频，进入音乐/图片/视频页会先停掉 BLE 主机
- 音频只支持 MP3 / WAV（不含 AAC / FLAC 独立文件）
- 蓝牙仅 BLE，不支持经典蓝牙与蓝牙音频（A2DP）
- EPUB 只提取文字，不渲染图片、CSS 与表格；加密（DRM）EPUB 不支持
- 暂不支持 PDF、MOBI、AZW3
- 图片按原尺寸解码，超大图片（超过 PSRAM 余量）会解码失败
- 中文字体占固件 7.1 MB，因此没有做 OTA 双分区
- 字号为 22 / 26 / 30 三档，34 px 已为视频功能移除
- 长文本按章节分页，打开大文件（>10 MB）时会占用较多 PSRAM
- 网页上传未做鉴权，请只在可信局域网内使用
- 章节识别基于常见命名（`第X章/节/回/卷`、`序章`、`Chapter` 等），无标记时按约 12 KB 自动分段

## 依赖与已知的构建问题

| 组件 | 版本 |
| --- | --- |
| ESP-IDF | 5.5.4 |
| waveshare/esp32_p4_wifi6_touch_lcd_4_3 | 1.0.1 |
| lvgl/lvgl | 9.5.0 |
| espressif/esp_hosted | 1.4.7 |
| espressif/esp_wifi_remote | 0.14.5 |
| espressif/esp_lv_decoder | 0.5.0 |
| espressif/esp_audio_codec | 2.5.0（<2.6，兼容 Rev1.3） |
| chmorgan/esp-audio-player | 1.1.0 |
| espressif/zlib | 1.3.2 |

`esp_hosted` 3.1.0（2026-11-05 发布）的组件清单中使用了 `$CONFIG{...} == 1` 形式的条件，
当前版本的组件管理器无法解析，会导致 `Missing required kconfig option after retry`。
因此工程通过 `main/Kconfig.projbuild` 补齐了 3 个同名 int 型符号（默认 0），
并把 `esp_hosted` / `esp_wifi_remote` 固定在 1.4.7 / 0.14.5。升级依赖前请先确认该问题已修复。

`esp_extractor` 是官方 `10_mp4_player` 内置的预编译静态库（`components/esp_extractor`，
约 2.9 MB，仅链接实际用到的符号），无需从组件管理器下载。

## 串口调试与自测

固件内置了一个串口命令行（UART0，115200），用于开发期逐页截图和硬件自检，
不影响正常触屏操作。命令以换行结尾：

| 命令 | 说明 |
| --- | --- |
| `ui <home\|library\|gallery\|music\|video\|wifi\|ble\|clock\|settings>` | 直接打开指定界面 |
| `shot` | 把当前帧缓冲以 RLE+base64 通过串口输出（配合 `tools/ui_probe.py`） |
| `theme <0-2>` | 切换主题并回首页（复现真实路径） |
| `ls <dir>` | 列目录，例如 `ls /sdcard/videos` |
| `wifiscan` | 扫描 2.4G 热点并打印 SSID/信号/加密方式 |
| `ble` / `blescan` | 初始化 BLE / 扫描 5 秒并打印发现的设备 |
| `wavtest` | 生成 `/sdcard/music/test.wav` 并播放（验证 ES8311） |
| `bmptest` | 生成 `/sdcard/pictures/test.bmp`（验证图片解码） |
| `mktest` | 把内置测试素材写到 `/pictures/test.jpg` 与 `/videos/test.avi` |
| `vidplay <path>` | 直接播放指定视频 |
| `gview <n>` | 打开图片列表第 n 张的全屏查看 |
| `heap` | 打印内部 RAM / DMA / PSRAM 余量 |

自动化自检脚本：

```powershell
python tools/ui_probe.py COM20 tools/.cache/ui            # 全部页面截图
python tools/ui_probe.py COM20 tools/.cache/ui media      # 图片/视频
python tools/ui_probe.py COM20 tools/.cache/ui diag       # 内存/BLE/音频
```

> `main/assets/test.jpg` 与 `test.avi` 仅用于自测（MJPEG 320×240，40 帧），
> 会随固件嵌入；不需要时从 `main/CMakeLists.txt` 的 `EMBED_FILES` 中移除即可。
> 截图速度受 115200 波特率限制，一张全屏图约需 30 秒。

## 排障

- **烧录时报 `bootloader.bin requires chip revision in range [v3.0 - v3.99]`**：
  板载芯片为 ESP32-P4 **Rev1.3**，而 `sdkconfig.defaults` 里必须是
  `CONFIG_ESP32P4_REV_MIN_100=y` + `CONFIG_ESP32P4_SELECTS_REV_LESS_V3=y` + `CONFIG_SPIRAM_SPEED_200M=y`
  （Rev3.x 芯片则改回 `CONFIG_ESP32P4_REV_MIN_300` 与 250 MHz PSRAM）。修改后删除 `sdkconfig` 重新 `idf.py build`
- **静态界面有残影**：官方示例用 `TRIPLE_PARTIAL` 三缓冲，但 MIPI DSI 下适配器拿不到
  `on_frame_buf_complete` 回调（启动日志会提示），静态界面会留下切缓冲造成的残影。
  本工程改用 `ESP_LV_ADAPTER_TEAR_AVOID_MODE_NONE`（单缓冲同步刷新），见 `main/main.c`
- **需要还原屏幕画面排查显示问题**：固件内置了截图输出，
  `python tools/capture_screen.py COM20 tools/.cache/shot.png` 会把当前帧缓冲通过串口读出并保存为 PNG
- **屏幕无显示 / 白屏**：确认使用 ESP-IDF ≥ 5.3（推荐 5.5.x），且芯片版本配置与实物一致
- **中文字糊成一团 / 部分汉字变成乱码**：两个独立原因，见上文“为什么必须拆包 + 非压缩”
- **点击按钮后蓝屏并重启**：这是 panic（`Guru Meditation Error`）。先用串口抓完整日志，
  再用 `riscv32-esp-elf-addr2line -pfiaC -e build/MP4.elf <MEPC> <RA>` 把地址翻译成源码行
- **提示“内置字体加载失败”**：`fonts/cjk_*.bin` 未生成或与 `fonts_manifest.h` 不匹配，
  执行 `python tools/gen_fonts.py --sizes 22 26 30` 后重新 `idf.py build flash`
- **改了字体但界面没变**：`main/CMakeLists.txt` 用 `GLOB` 收集字体，新增/删除字体文件后
  需要重新执行一次 `idf.py build`（触发 CMake 重新 configure）
- **WiFi 连不上**：确认是 2.4 GHz 网络；ESP32-C6 模组需要板载出厂固件，若被擦除需按官方文档恢复
- **蓝牙页一直显示“正在初始化”或提示失败**：说明 C6 固件未启用 BLE HCI；
  正常启动日志应出现 `vhci_drv: Host BT Support: Enabled` 与 `transport: - BLE only`
- **视频无法播放 / 只有声音**：确认容器与编码（需 MJPEG 视频轨），H.264 会直接拒绝；
  分辨率越高越吃 PSRAM，建议不超过 480×800
- **SD 卡不识别**：建议 FAT32；启动日志中 `storage: SD 卡已挂载到 /sdcard` 表示成功
