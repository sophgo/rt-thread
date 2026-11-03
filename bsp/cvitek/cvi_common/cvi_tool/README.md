# Cvitek 通用测试及工具集

本目录包含用于 Cvitek SoC 的通用测试程序和命令行工具，已集成到 RT-Thread 操作系统中。这些工具的编译受 Kconfig 选项控制。

## 目录结构

```
cvi_tool/                 # 命令行工具的源文件
├── devmem_tool.c      # 内存读写工具 (devmem)
├── i2c_tool.c         # I2C 工具 (i2cdetect, i2cget, i2cset)
├── wdt_tool.c         # 看门狗控制工具 (watchdog, boot_reason)
|── cvi_pinmux/        # Pinmux (引脚复用) 配置工具
|   ├── chip_cv180x/
|   │   └── cvi_pinmux.c # 适用于 CV180x 系列的 Pinmux 工具实现
|   └── chip_cv181x/
|       └── cvi_pinmux.c # 适用于 CV181x 系列的 Pinmux 工具实现
|
|── netutils-v1.3.3/        # Pinmux (引脚复用) 配置工具
|   ├── iperf/
|   │   └── iperf.c     # iperf 工具的实现
|   ├── telnet/
|   │   └── telnet.c    # telnet 服务端的实现（远程 msh shell）
|   └── tools/          # 包含了两个在上位机上使用的工具：jperf.rar、Tftpd64-4.60-setup.exe
|
|── littlefs-v2.11.2/       # littlefs 片上持久化文件系统（vendor 自 RT-Thread-packages/littlefs）
|   ├── lfs.c               # littlefs 上游核心实现
|   ├── dfs_lfs.c           # RT-Thread DFS_V2 适配层
|   ├── lfs_config.h        # lfs 桥接配置（printf/assert → rt_kprintf/RT_ASSERT）
|   └── host/               # 宿主机工具 mklfsimg.c：出厂预置文件打包（编译期把 customization/<project>/data/ 打成 littlefs 镜像）
|
└── SConscript             # 本模块的构建脚本
```

## 功能特性

### 命令行工具

这些工具在 Kconfig 中使能 `BSP_USING_DRIVER_TOOL` 后会被编译。它们提供了用于硬件交互和调试的 MSH shell 命令。

1.  **设备内存访问 (`devmem_tool.c`)**
    *   提供 `devmem` 命令，用于读取或写入物理内存地址。
    *   **Kconfig 选项:** `BSP_USING_DRIVER_TOOL`

2.  **Pinmux 工具 (`tools/cvi_pinmux/`)**
    *   提供 `cvi_pinmux` 命令，用于配置和查询引脚功能。
    *   根据 SoC 型号选择特定芯片的实现：
        *   `SOC_TYPE_CV180XB_QFN`: 编译 `tools/cvi_pinmux/chip_cv180x/cvi_pinmux.c`
        *   `SOC_TYPE_CV181XC_QFN` 或 `SOC_TYPE_CV181XH_BGA`: 编译 `tools/cvi_pinmux/chip_cv181x/cvi_pinmux.c`
    *   **Kconfig 选项:** `BSP_USING_DRIVER_TOOL` 和 `BSP_USING_PINMUX`，以及相关的 `SOC_TYPE_*` 选项。

3.  **I2C 工具 (`tools/i2c_tool.c`)**
    *   提供 `i2cdetect`, `i2cget`, `i2cset` 命令，用于 I2C 总线操作。
    *   **Kconfig 选项:** `BSP_USING_DRIVER_TOOL` 和 `BSP_USING_I2C_TOOL`。

4.  **看门狗工具 (`wdt_tool.c`)**
    *   提供 `watchdog` 看门狗配置、喂狗、停止喂狗、停用及状态查询命令，并提供 `boot_reason` 复位原因查询。
    *   **Kconfig 选项:** `BSP_USING_DRIVER_TOOL`、`BSP_USING_WDT_TOOL` 和 `BSP_USING_WDT`。

5.  **NETUTILS 工具 (`cvi_tool/netutils-v1.3.3`)**
    *   提供 `iperf`命令，用于网络测试。
    *   提供 `telnet_server` 命令，用于启动 telnet 服务端，通过网络远程访问 msh shell。
    *   **Kconfig 选项:** `BSP_USING_DRIVER_TOOL` 和 `BSP_USING_NETUTILS` ，具体的测试工具需要另外进行选择，比如选择`PKG_NETUTILS_IPERF`使能iperf、选择`PKG_NETUTILS_TELNET`使能telnet。

6.  **littlefs 片上持久化文件系统 (`cvi_tool/littlefs-v2.11.2`)**
    *   将分区表中的 `data` 分区以 littlefs 格式挂载到 `/data`，提供片上 SPI NOR 持久化存储（参数持久化等场景）。首次上电空分区自动格式化并重挂载。
    *   上游来源：RT-Thread-packages/littlefs（BSD-3-Clause），版本 v2.11.2 + lseek 签名修复（bf5beae）。
    *   **Kconfig 选项:** `BSP_USING_LITTLEFS`（依赖 `RT_USING_DFS_V2`，默认关闭）。
    *   **前提:** 板级分区表 XML 中需包含 `data` 分区（`fal_cfg.h` 构建时自动生成）。
    *   **出厂预置:** project 的 `customization/<project>/data/` 目录下放入文件（算法模型/配置），编译期自动打进 littlefs 镜像随固件烧入 DATA 分区（每个 project 即每块板卡一个 data 目录，随分区表所在目录确定；`host/mklfsimg.c` 按需现编）。详见 SDK 文档《littlefs /data》「出厂预置文件」章节。

## 构建说明

本目录中的组件作为 RT-Thread 主构建过程的一部分进行构建。请通过 Kconfig (例如，使用 `menuconfig`) 来使能所需的功能。

1.  使能通用驱动工具：
    *   `Project Configuration` -> `Cvitek Drivers Configuration` -> `ENABLE DRIVER_TOOLS`
2.  根据需要在 `ENABLE DRIVER_TOOLS`(`BSP_USING_DRIVER_TOOL`) 下使能特定的工具。例如：
    *   要使能 cvi_pinmux 工具：使能 `Enable CVI_PINMUX` (`BSP_USING_PINMUX`)。
    *   要使能 I2C 工具：使能 `Enable I2C_TOOL` (`BSP_USING_I2C_TOOL`)。
    *   要使能看门狗工具：开启 WDT 驱动 (`BSP_USING_WDT`) 后，使能 `Enable Watchdog tool` (`BSP_USING_WDT_TOOL`)。
    *   要使能 iperf 工具：使能 `EEnable NetUtils tools` (`BSP_USING_NETUTILS`)。

## 使用方法

编译并烧录到目标设备后，可以通过 MSH shell 调用这些工具。例如：

```msh
msh />devmem 0x03001000
msh />cvi_pinmux -r SD0_CLK
msh />i2cdetect 0
msh />watchdog -h
msh />boot_reason
```

有关详细的使用说明，请参考各个命令的帮助信息。
```msh
msh />devmem
msh />cvi_pinmux
msh />i2cdetect
```

NetUtils软件包的更详细的使用说明，请参考软件包内各个命令的帮助信息。

### telnet 使用注意事项

在 MSH 中执行 `telnet_server` 启动服务端后，使用 telnet 客户端连接开发板（默认端口 23）。以 MobaXterm 为例：

1. 新建 Telnet 会话，填入开发板 IP。
2. 在会话设置中打开 `Advanced Telnet settings`（高级 Telnet 设置）。
3. 将 `Telnet client type`（Telnet 客户端类型）从默认的 `PuTTY telnet` 改为 **`Busybox telnet`**。

否则敲回车时会出现乱码/换行异常的问题（msh shell 期望 `\n` 换行，而 PuTTY telnet 模式发送的回车序列不匹配）。
