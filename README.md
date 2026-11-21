# SmartTone

SmartTone 是一款基于 STM32F103 和 128×160 ST7735 彩屏的七键电子琴，使用 EC11 旋钮和 FUNC 键操作，外接 W25Q64 存储录音和媒体素材。

- **电子琴**：七键演奏、双声部合成、五种音色、音区与移调设置、五档音量及可调节拍器。
- **录音和曲库**：支持录音、加录和回放，录制时可用 FUNC 和旋钮调整参数。内置《青花瓷》《发如雪》《路小雨》等曲目，部分曲目支持变速。
- **音乐游戏和自动作曲**：七键节奏游戏有三档难度，自动作曲可生成并试听旋律。
- **DS 音乐小屋**：弹琴时屏幕琴键随按键高亮。短按 FUNC 进入舞蹈，向任一方向转一格切换普通舞蹈和醒狮舞，再按 FUNC 返回弹琴。

![DS 音乐小屋醒狮舞（主机模拟）](docs/ds-room.png)

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
