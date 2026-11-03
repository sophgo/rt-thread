# libraries — IP 寄存器/厂商库层

本目录是 BSP 的 **IP 寄存器/厂商库层**，与 `../drivers/`（RT-Thread 适配层）平级：

- `drivers/<module>/drv_*.c` 只做设备框架适配（注册设备、ops 表、UAPI 入口、ISR 胶水）；
  寄存器级实现（mmio 读写、IP 编排、位域、时序等待）下沉到本目录 `libraries/<ip>/`。
- 消费者：`drivers/` 各模块（src 相对路径收编库源）、`components/cvi_comps`（媒体组件）、
  `cvi_rtt` 板级代码（经全局 CPPPATH 解析 `hal_*.h` 等契约头）。
- 根目录的 `mmio.h`、`hal_barrier.h`、`hal_debug.h`、`hal_def.h`、`hal_dma.h`、`hal_list.h` 是跨模块契约头。

## 芯片差异规范

- **有芯片差异的库**：主实现放 `common/`，芯片差异（常量、数据表、差异源文件）放
  `chip/<chip>/`。同名 chip 头由 SConscript 按 `chip_type` 选进 include 路径，同一时刻
  只有一个 chip 目录在路径上，消费方 include 时不感知芯片目录。
  范例：`pinctrl`（pinmux 表）、`tpu`（TIU lane 数：cv180x=2，cv181x=8）。
- **无芯片差异的库**：单份维护，直接放 `libraries/<ip>/`。按芯片复制零差异代码是反模式，
  禁止为"看起来统一"而复制。
- **新增库先判断是否存在芯片差异**再选形态：
  `grep -rn "__CV180X__\|__CV181X__\|__CV184X__\|SOC_TYPE_\|BOARD_TYPE_" <源码>` 有命中
  → `common/` + `chip/<chip>/`；零命中 → 单份。
- 已知遗留：`spiflash/src/spiflash.c:24-38` 两处 `#if`（SPINOR 引脚组按芯片选择），
  待后续迁移到 `chip/` 形态。

## 现有库清单

| 库 | 芯片差异 | 形态 |
| --- | --- | --- |
| adc | 无 | 单份（cvi_adc） |
| codec | 无 | 单份（phy adc/dac） |
| dma | 无 | 单份（dw_dma） |
| efuse | 无 | 单份（cvi_efuse） |
| eth | 无 | 单份（dw_eth_mac / ephy / eth_phy） |
| gpio | 无 | 单份（dwapb_gpio） |
| i2c | 无 | 单份（dw_iic） |
| i2s | 无 | 单份（dw_i2s） |
| pwm | 无 | 单份（cvi_pwm） |
| reset | 无 | 单份（cvi_reset） |
| rtc | 无 | 单份（cvi_rtc） |
| sdif | 无 | 单份（dw_mmc 寄存器布局） |
| spacc | 无 | 单份（cvi_spacc） |
| spi | 无 | 单份（dw_spi） |
| timer | 无 | 单份（cvi_timer） |
| uart | 无 | 单份（dw_uart） |
| wdt | 无 | 单份（cvi_wdt） |
| pinctrl | pinmux 表按芯片 | `common/` 无（纯数据）；`chip/<chip>/` 差异表 |
| tpu | TIU lane 数（cv180x=2，cv181x=8） | `common/`（cvi_tpu.c / cvi_tpu_regs.h / hal_tpu.h）+ `chip/<chip>/cvi_tpu_chip.c` |
| spiflash | SPINOR 引脚组（`#if` 遗留） | 单份（include/ + src/，待迁移） |

## SConscript 接入要点

库源不单独建组，由对应模块 SConscript 收编（范例：`drivers/tpu/SConscript`）：

- 模块 SConscript 顶部定义 `LIB_ROOT = os.path.join(driver_root, '..', 'libraries')`
  （`driver_root` 为 `bsp/cvitek/drivers`），CPPPATH 一律经 `LIB_ROOT` 拼。
- src 用相对路径字符串 `'../../libraries/<ip>/x.c'`（模块目录在 `drivers/<module>/`，
  深两级）——相对串经 variant 重映射，`.o` 落 `build/<sol>_<proj>/libraries/<ip>/`，
  不逃逸源码树。顶层 `drivers/SConscript` 引用库时只上跳一层（`'../libraries/...'`），
  且必须显式枚举、不能用 Glob（跨 variant 根 Glob 返回空列表，会静默丢源、链接期才暴露）。
- 有 `chip/` 子目录的库：`chip_type` 按 `GetDepend('__CV181X__' / '__CV180X__')` 判定
  （默认 `cv180x`），src 拼
  `'../../libraries/<ip>/chip/' + chip_type + '/xxx.c'`，CPPPATH 加
  `os.path.join(LIB_ROOT, '<ip>', 'common')` 与
  `os.path.join(LIB_ROOT, '<ip>', 'chip', chip_type)`。
- `libraries/` 整树有自己的 variant mount（本目录 `SConscript` 锚点 +
  `cvi_rtt/SConstruct` 挂载），模块相对串 src 依赖该挂载才能解析。
