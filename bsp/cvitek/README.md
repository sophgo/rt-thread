<!-- 简体中文 | [English](README-en.md) -->

# 目录

- [目录](#目录)
- [项目简介](#项目简介)
  - [处理器规格](#处理器规格)
  - [硬件资料](#硬件资料)
- [SDK 目录结构](#sdk-目录结构)
  - [Solutions 说明](#solutions-说明)
    - [解决方案目录结构](#解决方案目录结构)
- [SDK 编译使用说明](#sdk-编译使用说明)
  - [编译环境准备](#编译环境准备)
    - [依赖安装](#依赖安装)
    - [编译工具链下载](#编译工具链下载)
  - [编译步骤及说明](#编译步骤及说明)
    - [编译步骤](#编译步骤)
    - [镜像文件说明](#镜像文件说明)
  - [固件烧录](#固件烧录)
  - [运行说明](#运行说明)
- [User Manual](#user-manual)
- [关于算能](#关于算能)

<br>

# 项目简介

- 本仓库提供[算能科技](https://www.sophgo.com/)端侧处理器`CV181x`和`CV180x`两个系列处理器的软件开发包(SDK)
- 主要适用于官方 EVB

<br>

## 处理器规格

- [处理器产品简介](https://www.sophgo.com/product/index.html)

<br>

## 硬件资料

- [《CV180xB EVB 板硬件指南》](https://sophon-file.sophon.cn/sophon-prod-s3/drive/23/03/14/14/CV180xB_EVB%E6%9D%BF%E7%A1%AC%E4%BB%B6%E6%8C%87%E5%8D%97_V1.0.pdf)
- [《CV180xC EVB 板硬件指南》](https://sophon-file.sophon.cn/sophon-prod-s3/drive/23/03/18/18/CV180xC_EVB%E6%9D%BF%E7%A1%AC%E4%BB%B6%E6%8C%87%E5%8D%97_V1.0.pdf)
- [《CV181xC EVB 板硬件指南》](https://sophon-file.sophon.cn/sophon-prod-s3/drive/23/03/15/14/CV181xC_QFN_EVB%E6%9D%BF%E7%A1%AC%E4%BB%B6%E6%8C%87%E5%8D%97_V1.0.pdf)
- [《CV181xH EVB 板硬件指南》](https://sophon-file.sophon.cn/sophon-prod-s3/drive/23/03/15/15/CV181xH_EVB%E6%9D%BF%E7%A1%AC%E4%BB%B6%E6%8C%87%E5%8D%97_V1.0.pdf)

<br>

# SDK 目录结构

RT-Thread 固件相关目录（`bsp/cvitek/` 下）：

- `drivers/` — RT-Thread 设备框架适配层，按外设模块分目录（`adc`/`gpio`/`i2c`/`uart`/...），
  每个模块的 `test/` 为其自测
- `libraries/` — 寄存器/IP 层实现，与 `drivers/` 平级；有芯片差异的库用
  `chip/<chip>/` 子目录承载（如 `pinctrl`、`tpu`）
- `cvi_common/cvi_tool/` — 命令行调试工具（`devmem`、`i2c_tool`、`perf_stat`、`cvi_pinmux` 等）
- `cvi_boards/` — 多芯片适配的板级公共代码（`board.c` / `board.h`，被各芯片工程共享）
- `cv18xx_boot`、`cv18xx_risc-v`、`c906_little` 等 — 引导固件/小核的独立 SDK 工程，
  日常应用构建不需要进入

应用与 solution 相关目录在 `cvi_rtt/` 下：

```
cvi_rtt
├── boards            # 芯片登记（chip_list.json）、Kconfig.chips、bootimgs 预编译引导镜像
├── components        # CVITEK 组件（cli / osdrv / mpi / panel / tdl / ...）
├── docs              # 烧录、OTA、boot 固件编译等专题文档
├── solutions         # 各 solution，其 customization/<project>/ 即一块板卡
├── tools             # 构建辅助（image_tools、gen_project_kconfig.py、ci）
├── envsetup.sh       # 推荐构建入口：defconfig / menuconfig / build_all
└── Makefile          # 备选入口：make <solution> PROJECT=<project>
```

<br>

## Solutions 说明

`cvi_rtt/solutions/` 下的每个目录是一个 solution（可理解为一种产品形态），
目前有 `smart_doorbell`、`mpi_sample`、`mpi_ut`、`peripherals_test`、`cv18xx_boot`。

每个 solution 的 `customization/<project>/` 是一块具体的板卡，例如 `smart_doorbell`
下有 `turnkey_180xb`、`turnkey_181xc`、`cv1801b_spinand`、`cv1810c_spinand`、
`cv1811c_spinand`、`cv1811c_dwt_spinor` 等。构建时用 `<solution>_<project>` 指定目标板卡。

<br>

### 解决方案目录结构

以 `solutions/smart_doorbell/customization/turnkey_181xc/` 为例：

```
turnkey_181xc
├── .config                 # 该板卡唯一配置真源（rtconfig.h 由它生成）
├── partition_spinor.xml    # 分区表（NAND/eMMC 板卡为 partition_spinand.xml / _emmc.xml）
├── param/                  # 各模块参数：param/sysparam/viparam/vpssparam/vencparam/voparam
├── src/                    # 板级代码，主要是 platform.c（引脚复用等）
└── data/                   # data 分区内容
```

新增板卡只需在 `customization/` 下建这个目录并放入上述文件，无需改 Kconfig——
`tools/gen_project_kconfig.py` 会在构建时扫描子目录自动生成 Project 选择项。

<br>

# SDK 编译使用说明

<br>

## 编译环境准备

### 依赖安装
  ```shell
  $ sudo apt install -y scons libncurses5-dev device-tree-compiler
  ```

### 编译工具链下载

在Release板块下载玄铁交叉编译工具链`Xuantie-900-gcc-elf-newlib-x86_64-V2.8.1`

解压后，将编译工具链文件夹通过 `ln -s path_to_host-tools ./host-tools` 将实际工具链链接至当前 SDK 的`host-tools`目录下

此时 `host-tools` 目录结构如下:
```shell
host-tools
└── Xuantie-900-gcc-elf-newlib-x86_64-V2.8.1
```
此时``RT-Thread``仓库根目录下的目录结构为
```
.
├── bsp
├── ChangeLog.md
├── components
├── documentation
├── examples
├── host-tools
├── include
├── Kconfig
├── libcpu
├── LICENSE
├── README_de.md
├── README_es.md
├── README.md
├── README_zh.md
├── solutions
├── src
└── tools
```
<br>

## 编译步骤及说明


<br>

### 编译步骤

RT-Thread 固件由 `cvi_rtt` 目录构建，推荐入口是 `envsetup.sh`：

```shell
$ cd cvi_rtt
$ source envsetup.sh
$ defconfig <solution>_<project>     # Tab 可补全，如 smart_doorbell_turnkey_180xb
$ menuconfig                        # 可选，改完自动回写 customization/<project>/.config
$ build_all
```

也可直接用 Makefile 入口：`make smart_doorbell PROJECT=turnkey_180xb`。

- `<solution>` 为 `solutions/` 下的目录名（`smart_doorbell`、`mpi_sample`、`mpi_ut`、
  `peripherals_test`、`cv18xx_boot`），`<project>` 为该 solution 的 `customization/` 子目录名
- `.config` 是配置的唯一真源，位于 `solutions/<solution>/customization/<project>/.config`；
  `rtconfig.h` 与 solution 根目录的 `.config` 均为构建期生成物，不入库
- 芯片系列（`cv180xb_qfn` / `cv181xc_qfn`）在 menuconfig 的 `Chip Series` 菜单中选择

引导镜像 `boot0` / `boot` / `fip.bin` 通常**不需要**重新编译：`cvi_rtt/boards/bootimgs/<series>/<arch>_<media>/`
下已备好各系列与启动介质的预编译镜像，构建时自动打包。确需重编时见
[cvi_rtt/docs/boot_firmware_build.md](../../cvi_rtt/docs/boot_firmware_build.md)。

<br>

### 镜像文件说明

编译产物位于 `cvi_rtt/install/<solution>_<project>/`：

- `image/` — 可整包烧录的目录
  - `boot0`、`boot`、`fip.bin`：引导镜像（由 `boards/bootimgs/` 拷入）
  - `rtt.bin`：RT-Thread 固件（由 `rtthread.elf` 转出并封 CIMG）
  - `partition_spinor.xml`：分区表（按启动介质为 `partition_spinand.xml` / `partition_emmc.xml`）
- `rawimages/` — 未经 CIMG 封装的原始镜像（`boot0` / `boot` / `rtt.bin`）
- `image.zip` — `image/` 的压缩包
- `rtthread.elf` / `rtthread.map` — 带符号的 ELF 与 map 文件

<br>

## 固件烧录

请参考 [CVI RT-Thread USB Burn](docs/usb_burn.md)。

**TF 卡烧录**

- TF卡格式化为FAT32
- 将 `cvi_rtt/install/<solution>_<project>/image/` 下的 `boot0`、`boot`、`fip.bin`、`rtt.bin`、`partition_spinor.xml` 拷贝至TF卡
- 设备断电后插入TF卡，重新上电，等待**Start SD downloading**烧录提示
- 烧录日志如下
  ```shell
    Start SD downloading...
    switch to partitions #0, OK
    mmc0 is current device
    spinor id = EF 40 18
    SF: Detected W25Q128JV-IQ with page size 256 Bytes, erase size 4 KiB, total 16 MiB

    +-----+--------------------------------+------------+------------+----------+
    | idx | file                           |     offset |       size |     size |
    +-----+--------------------------------+------------+------------+----------+
    |   0 | boot0                          | 0x00000000 | 0x0000c000 |    48 KB |
    |   1 | boot                           | 0x0000c000 | 0x00020000 |   128 KB |
    |   2 | rtt.bin                        | 0x0002c000 | 0x003d2000 |  3912 KB |
    +-----+--------------------------------+------------+------------+----------+

    25728 bytes read in 5 ms (4.9 MiB/s)
    boot0: CIMG header found, skip 128 bytes, fip 0x6400
    device 0 offset 0x0, size 0x6400
    24576 bytes written, 1024 bytes skipped in 0.298s, speed 86802 B/s
    sf update speed 0.83 MB/s
    Program boot0 done
    64 bytes read in 4 ms (15.6 KiB/s)
    Header Version:1
    81936 bytes read in 9 ms (8.7 MiB/s)
    SF: 131072 bytes @ 0xc000 Erased: OK
    device 0 offset 0xc000, size 0x13fd0
    SF: 81872 bytes @ 0xc000 Written: OK
    sf write speed 0.610 MB/s
    64 bytes read in 4 ms (15.6 KiB/s)
    Header Version:1
    2456776 bytes read in 114 ms (20.6 MiB/s)
    SF: 4005888 bytes @ 0x2c000 Erased: OK
    device 0 offset 0x2c000, size 0x257c88
    SF: 2456712 bytes @ 0x2c000 Written: OK
    sf write speed 0.638 MB/s
    Saving Environment to SPIFlash... Erasing SPI flash...Writing to SPI flash...done
    OK
    cv181x_c906#
  ```

**USB 烧录**

请参考 [CVI RT-Thread USB Burn](docs/usb_burn.md)。

<br>

## 运行说明


- 上电有如下打印证明内核启动完毕

  ```shell
    Starting kernel ...

    [I/drv.pinmux] Pin Name = "UART0_RX", Func Type = 281, selected Func [0]

    [I/drv.pinmux] Pin Name = "UART0_TX", Func Type = 282, selected Func [0]

    heap: [0x80291698 - 0x81200000]

    \ | /
    - RT -     Thread Operating System
    / | \     5.2.0 build Dec 11 2024 14:35:24
    2006 - 2024 Copyright by RT-Thread team
    lwIP-2.1.2 initialized!
    [I/sal.skt] Socket Abstraction Layer initialize success.
    Hello RISC-V!
    msh />
    msh />
    msh />
  ```

- 输入串口回车可以正常输入输出，并且有 cli_uart 打印即运行正常

<br>

# User Manual

- [boot0 & boot & fip.bin 固件编译](../../cvi_rtt/docs/boot_firmware_build.md)
- [CVI RT-Thread USB Burn](docs/usb_burn.md)
- [BOOT USB CDC OTA 使用手册](../../cvi_rtt/docs/cdc_ota_manual.md)

# 关于算能

- 算能致力于成为全球领先的定制算力提供商，专注于 RISC-V、TPU 处理器等算力产品的研发和推广应用。
- 公司遵循全面开源开放的生态理念，携手行业伙伴推动 RISC-V 高性能通用计算产业落地；打造覆盖“云、边、端”的全场景产品矩阵，为数据中心、AIGC、城市运营、智能制造、智能终端等多元场景提供算力产品及整体解决方案。
- 算能在北京、上海、深圳、青岛、厦门等国内 10 多个城市及美国、新加坡等国家设有研发中心。
- 自 2016 年以来，旗下品牌算丰 SOPHON 系列产品已完成多次迭代，每代产品相较于前代产品均实现了能耗比倍数级提升。
- 官方网站：https://www.sophgo.com/
- 开发社区：https://developer.sophgo.com/forum/index.html

