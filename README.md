# SmartTone

SmartTone 是一款基于 STM32F103 和 128×160 ST7735 彩屏的七键电子琴，使用 EC11 旋钮和 FUNC 键操作，外接 W25Q64 存储录音和媒体素材。

- **电子琴**：七键演奏、双声部合成、五种音色、音区与移调设置、五档音量及可调节拍器。
- **录音和曲库**：支持录音、加录和回放，录制时可用 FUNC 和旋钮调整参数。内置《青花瓷》《发如雪》《路小雨》等曲目，部分曲目支持变速。
- **音乐游戏和自动作曲**：七键节奏游戏有三档难度，自动作曲可生成并试听旋律。
- **DS 音乐小屋**：弹琴时屏幕琴键随按键高亮。短按 FUNC 进入舞蹈，向任一方向转一格切换普通舞蹈和醒狮舞，再按 FUNC 返回弹琴。

![DS 音乐小屋醒狮舞（主机模拟）](docs/ds-room.png)

## 硬件接线

表中的 PA、PB 是 STM32 板上标注的 GPIO。所有模块共地，ST7735 和 W25Q64 使用 3.3 V 电源及逻辑电平。

### 琴键和 EC11

| 部件 / 信号 | STM32 引脚 |
| --- | --- |
| Do / Re / Mi / Fa | PA1 / PA2 / PA3 / PA4 |
| Sol / La / Si | PA11 / PA10 / PA9 |
| FUNC | PA8 |
| EC11 A / B（CLK / DT） | PB6 / PB7 |
| EC11 按压开关（SW） | PB13 |

琴键和 FUNC 的另一端接 GND。EC11 的 A/B 公共端和按压开关的另一端也接 GND。这些输入使用内部上拉，按下为低电平。带 VCC 的 EC11 模块使用 3.3 V。

### ST7735 屏幕和 W25Q64

| 信号 | ST7735 | W25Q64 | STM32 引脚 / 电源 |
| --- | --- | --- | --- |
| 供电 | VCC | VCC | 3.3 V |
| 地 | GND | GND | GND |
| SPI1 时钟 | SCK / SCL | CLK / SCK | PA5 |
| SPI1 主控输出 | MOSI / SDA / DIN | DI / MOSI | PA7 |
| SPI1 主控输入 | 此工程无需连接 | DO / MISO | PA6 |
| 独立片选 | CS | — | PB5 |
| 独立片选 | — | CS / CS# | PB0 |
| 数据 / 命令 | DC / A0 | — | PB10 |
| 屏幕复位 | RES / RST | — | PB11 |
| 背光调光 | BL / BLK 控制端 | — | PB12 |

屏幕和 Flash 共用 SPI1，片选分别接线。裸 W25Q64 若引出 `WP#` 和 `HOLD#`，两脚上拉到 3.3 V。PB12 已用于背光，旧版的 Do 指示灯接线不适用。若屏幕只引出裸背光 LED，请通过模块所需的限流或驱动电路接入。

### 音频和烧录

| 信号 | STM32 引脚 | 接法 |
| --- | --- | --- |
| 电子琴音频 | PA0（TIM2_CH1） | 串联 1 kΩ，电阻后的节点接 10 nF 到 GND，再经 4.7 µF 耦合到 HXJ8002 音频输入 |
| 节拍器 | PB1（TIM3_CH4） | 接低电平有效无源蜂鸣器模块的 SIG / IN，信号脚须兼容 3.3 V |
| ST-Link SWDIO | PA13 | SWDIO 接 SWDIO |
| ST-Link SWCLK | PA14 | SWCLK / SWCK 接 SWCLK |
| ST-Link GND | GND | 与主控共地 |

HXJ8002 用 5 V 供电并和主控共地。基线扬声器为 8 Ω / 2 W，两端分别接功放的 OUT+ 和 OUT−，两端均不接 GND。PB8、PB9 目前空闲。

烧录前确认供电方式。如果由 ST-Link 的 3.3 V 输出给目标板供电，先断开 DC。若调试器标的是 VTref，请按其说明接目标板的参考电压。切回 DC 供电前，先断开 ST-Link。

固件引脚定义可对照 [main.h](Core/Inc/main.h)、[SPI1 配置](Core/Src/stm32f1xx_hal_msp.c)和[背光驱动](BSP/Src/bsp_tft.c)。

## 构建与测试

在 Windows PowerShell 中使用 Arm GNU Toolchain，将 `arm-none-eabi-gcc` 加入 PATH，或将 `ARM_TOOLCHAIN_PATH` 指向工具链的 `bin` 目录。

```powershell
.\tools\build.ps1 -Configuration Release
.\tools\test_host.ps1 -Compiler gcc
.\tools\check_glyphs.ps1
```

主控烧录文件生成在 `build/STM32F103_TFT_ST7735.hex`。本工程按 **128 KiB 内部 Flash** 链接，烧录前请核对目标芯片容量。主机测试也可用 Zig，将 `-Compiler` 指向 `zig.exe`。

## 外部 Flash 素材

新板需另行写入 W25Q64 素材。`tools/write_c4_asset.py` 使用 ST-Link 和 OpenOCD 烧录并回读校验，每次用不同的 `--output` 目录保存备份和日志。

| 素材 | 起始地址 | 工具参数 |
| --- | --- | --- |
| C4 PCM 音频 | `0x100000` | 默认 |
| DS 普通舞蹈 | `0x200000` | `--ds-dance` |
| DS 醒狮舞 | `0x208000` | `--ds-lion` |

固件源码位于 `Core/`、`BSP/` 和 `Drivers/`，外部 Flash 素材位于 `assets/`。
