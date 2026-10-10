# ESP32-P4 2.1" AMOLED LVGL 开发板示例

这是 ESP32-P4 + 2.1 寸 480×480 AMOLED 开发板的基础示例工程，纯 ESP-IDF 编写，不依赖 Arduino。

- 显示：CO5300，MIPI-DSI 1 lane，LVGL 9
- 触摸：CST9220（I2C）
- 4 个物理按键
- 无源蜂鸣器
- TF 卡（SDMMC 4-bit）

上电后显示 LVGL 官方 Widgets demo。点右下角的 ⚙ 按钮，或者**长按 RETURN 键**，可以进入硬件测试页面：

| 测试项 | 怎么测 |
| --- | --- |
| 按键 | 按任意按键，对应方块会亮起，计数加一，蜂鸣器发出不同音高。还会显示按下、松开、长按事件 |
| 蜂鸣器 | 打开或关闭按键音，拖动音量条，试听 1k / 2.6k / 4k，播放一小段旋律 |
| TF 卡 | 开机自动挂载。可以查看卡名、类型、容量和剩余空间，也可以挂载、卸载、做 1MB 读写测速和校验，并列出根目录文件 |

在测试页面里，UP / DOWN 键可以上下滚动。

## 引脚定义

所有引脚都定义在 [`components/board/include/board_config.h`](components/board/include/board_config.h)。

| 功能 | 信号 | GPIO | 说明 |
| --- | --- | --- | --- |
| 屏幕 | LCD_RST | 27 | |
| | TE | 20 | 预留，示例未使用 |
| | MIPI DSI | 专用引脚 | 1 lane，320 Mbps，PHY 由片内 LDO3 供 2.5V |
| 触摸 | I2C SDA | 7 | I2C0，400 kHz |
| | I2C SCL | 8 | |
| | RST / INT | 未接 | 轮询读取 |
| 按键 | KEY1 CONFIRM | 47 | 按下接地，内部上拉 |
| | KEY2 DOWN / RIGHT | 4 | |
| | KEY3 UP / LEFT | 5 | |
| | KEY4 RETURN | 35 | strapping 脚，上电时不要按住 |
| 蜂鸣器 | BEEP | 48 | 无源，经三极管驱动，低电平响 |
| TF 卡 | CLK | 43 | SDMMC 4-bit，IO 电源由片内 LDO4 提供 |
| | CMD | 44 | |
| | D0 / D1 / D2 / D3 | 39 / 40 / 41 / 42 | |
| UART1 | RX / TX | 46 / 32 | 示例未使用 |
| UART2 | RX / TX | 21 / 22 | 示例未使用 |
| 扩展 I2C | SDA / SCL | 3 / 2 | 示例未使用 |

## 工程结构

```
├── main/
│   ├── main.c              # 入口：初始化外设 -> LVGL demo -> 测试页
│   └── hw_test_ui.c        # 硬件测试页面（按键 / 蜂鸣器 / TF 卡）
└── components/board/       # 板级支持包，可以直接复制到自己的工程
    ├── include/
    │   ├── board_config.h      # ★ 全部引脚和屏幕参数
    │   ├── board_display.h     # 屏幕
    │   ├── board_touch.h       # 触摸
    │   ├── board_lvgl.h        # LVGL 移植（含线程锁）
    │   ├── board_button.h      # 按键
    │   ├── board_beeper.h      # 蜂鸣器
    │   └── board_sdcard.h      # TF 卡
    └── src/
        ├── board_display.c         # CO5300 MIPI-DSI 驱动
        ├── co5300_init_cmds.h      # 屏幕初始化指令
        ├── esp_lcd_touch_cst9220.c # CST9220 触摸驱动
        ├── board_touch.c
        ├── board_lvgl.c
        ├── board_button.c
        ├── board_beeper.c
        └── board_sdcard.c
```

## 编译和烧录

需要 **ESP-IDF v5.5 或更新版本**。第一次编译时会自动下载 `lvgl/lvgl 9.4` 和 `espressif/esp_lcd_touch`。

```bash
idf.py set-target esp32p4
idf.py build
idf.py -p COMx flash monitor
```

使用 VS Code ESP-IDF 插件时：先选目标 `esp32p4`，再点 Build 和 Flash。

> 修改了 `sdkconfig.defaults` 以后，要先删除 `sdkconfig`，重新编译才会生效。

## 二次开发

**在自己的工程里使用 LVGL**：LVGL 运行在独立任务中。在其他任务里调用任何 `lv_xxx` 函数之前，都要先加锁：

```c
board_lvgl_lock(0);
lv_label_set_text(label, "hello");
board_lvgl_unlock();
```

**换一个 demo**：在 `main.c` 里把 `lv_demo_widgets()` 换成 `lv_demo_music()`、`lv_demo_benchmark()` 等，并在 menuconfig → LVGL → Demos 里打开对应选项。

**按键事件**：

```c
static void on_key(board_key_id_t key, board_key_event_t ev, void *ctx)
{
    if (key == BOARD_KEY_ID_CONFIRM && ev == BOARD_KEY_EVENT_PRESS) { ... }
}
board_button_init(on_key, NULL);
```

**蜂鸣器**：`board_beeper_beep(freq_hz, ms)` 调用后立即返回，不会卡住界面。

**TF 卡**：调用 `board_sdcard_mount()` 后，直接用标准 C 的 `fopen("/sdcard/xxx.txt", "w")` 读写文件。

**屏幕方向**：修改 `board_config.h` 里的 `BOARD_LCD_ROTATION_DEG`（0 / 90 / 180 / 270）。触摸坐标会跟着自动转换。

## 说明

这个示例的显示驱动是最基础的写法：单帧缓冲，由 DMA2D 把 LVGL 局部刷新的结果拷进显存，重点是好懂、好改，没有做额外的刷新率优化。
