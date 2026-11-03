# SPACC Test Script 使用说明和指南

## 概述

SPACC Test Script 是一个基于 Python 的自动化测试工具，用于测试 RT-Thread 系统中的 SPACC (Security Processing Accelerator) 加密硬件模块。该脚本通过串口与目标设备通信，执行各种加密算法测试，并与 Python 的 `cryptography` 库进行结果比对验证。

## 环境准备

### 依赖安装

```bash
pip install pytest pyserial cryptography
```

### 硬件连接

1. 将目标设备通过 USB 转串口线连接到测试主机
2. 确认串口设备在系统中可识别（如 `/dev/ttyUSB0` 或 `COM3`）

## 使用方法

### 配置串口参数

在 `spacc_test_script.py` 文件中的spacc_client() 测试固件函数中修改以下参数：

```python
# 修改为实际串口设备和波特率
PORT = "COM3"  # Change to your serial port
BAUD_RATE = 115200
```

常见的串口设备名：
- Windows: `COM1`, `COM2`, `COM3` 等
- Linux: `/dev/ttyUSB0`, `/dev/ttyACM0` 等

### 基本测试运行

⚠️ **重要注意事项**

在运行 SPACC 测试脚本时，请确保**调试串口没有被其他程序占用**，需要先断开，否则无法正常测试。

```bash
# 运行所有测试
pytest spacc_test_script.py

# 运行特定算法测试
pytest spacc_test_script.py::test_spacc_aes
pytest spacc_test_script.py::test_spacc_sm4
pytest spacc_test_script.py::test_spacc_tdes
pytest spacc_test_script.py::test_spacc_sha1
pytest spacc_test_script.py::test_spacc_sha256
pytest spacc_test_script.py::test_spacc_base64
```

## 测试内容说明

### 对称加密算法测试

支持以下对称加密算法和模式：

1. **AES**
   - 密钥长度：128/192/256 bits (16/24/32 字节)
   - 模式：ECB/CBC/CTR
   - 默认测试次数：`TEST_CASE_NUM` 次

2. **SM4**
   - 密钥长度：128 bits (16 字节)
   - 模式：ECB/CBC/CTR
   - 默认测试次数：`TEST_CASE_NUM` 次

3. **TDES**
   - 密钥长度：192 bits (24 字节)
   - 模式：ECB/CBC
   - 默认测试次数：`TEST_CASE_NUM` 次

### 哈希算法测试

1. **SHA1**
   - 输出长度：160 bits (20 字节)
   - 默认测试次数：`TEST_CASE_NUM` 次

2. **SHA256**
   - 输出长度：256 bits (32 字节)
   - 默认测试次数：`TEST_CASE_NUM` 次

3. **SM3**
   - 输出长度：256 bits (32 字节)
   - 当前标记为跳过状态（未实现）

### Base64 编解码测试

- 默认测试次数：`TEST_CASE_NUM` 次

## 测试执行流程

### 对称加密测试流程

1. 在主机端使用 `cryptography` 库生成参考加密结果
2. 通过串口向目标设备发送测试命令
3. 目标设备执行加密操作并返回结果
4. 比对参考结果与设备返回结果
5. 执行解密操作验证加解密一致性

### 哈希测试流程

1. 在主机端使用 `cryptography` 库生成参考哈希值
2. 通过串口向目标设备发送测试命令
3. 目标设备计算哈希值并返回结果
4. 比对参考结果与设备返回结果

### Base64 测试流程

1. 在主机端使用 Python 标准库生成参考编码结果
2. 通过串口向目标设备发送测试命令
3. 目标设备执行 Base64 编解码操作并返回结果
4. 比对参考结果与设备返回结果
5. 验证编解码一致性

## 测试命令格式

### 对称加密测试命令

```bash
# spacc_test_symm_cipher -算法 -m 模式 -k "密钥" -in "明文" [-iv "初始向量"]
spacc_test_symm_cipher -aes -m 1 -k "mykey123456789012" -in "hello world" -iv "myiv123456789012"
```

### 哈希测试命令

```bash
# spacc_test_hash -算法 -in "消息"
spacc_test_hash -sha256 -in "hello world"
```

### Base64 测试命令

```bash
# spacc_test_base64_dynm -in "数据"
spacc_test_base64_dynm -in "hello world"
```

## 参数配置

### 测试次数调整

修改 [spacc_test_script.py](./spacc_test_script.py) 中的 [TEST_CASE_NUM](./spacc_test_script.py#L22-L22) 常量：

```python
TEST_CASE_NUM = 10  # 可调整为其他数值
```

### 数据长度限制

修改 [spacc_test_script.py](./spacc_test_script.py) 中的常量来修改随机生成的测试用例长度：

```python
# 随机生成的测试用例最小长度、最大长度
MIN_PLAINTEXT_LEN, MAX_PLAINTEXT_LEN = 1, 55
```

## 输出信息

测试过程中会输出以下信息：

1. 测试命令详情
2. 输入数据（密钥、明文等）
3. 期望结果与实际结果
4. 测试通过/失败状态
5. 详细响应日志

## 故障排除

### 设备问题

1. 设备是否支持 SPACC 加密硬件
2. 检查设备 FINSH/MSH 命令行功能是否正常
3. ⚠️ **!!命令行缓冲区大小是否足够!!**，如 `CONSOLEBUF_SIZE`、`FINSH_CMD_SIZE`
4. ⚠️ **!!命令行最大可解析长度是否足够!!**，如 `FINSH_ARG_MAX`

### 串口连接问题

1. 检查串口线连接是否正常
2. 确认串口设备名是否正确
3. 检查串口是否被其他程序占用
4. 确认目标设备串口配置（波特率 115200）

### 测试失败处理

1. 查看详细响应日志分析问题
2. 确认目标设备固件版本
3. 检查输入数据格式是否正确
4. 验证硬件模块是否正常工作

### 特殊情况

- Base64 测试会自动跳过长度超过 48 字节的测试用例
- SM3 哈希算法测试默认被跳过（未实现）

## 自定义测试

可以通过修改 [spacc_test_script.py](./spacc_test_script.py) 中的以下内容来自定义测试：

1. 测试用例生成函数
2. 参数范围和限制
3. 测试断言条件
4. 输出日志格式

## 注意事项

1. 确保目标设备已正确烧录包含 SPACC 驱动的固件
2. 测试前确认设备串口功能正常
3. 长时间运行大量测试需注意设备响应的性能
4. 根据实际硬件能力调整测试参数和次数