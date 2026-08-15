# CAN-OTA 现有文件修改指南

本文档说明如何将 CAN-OTA 代码集成到现有工程中。

---

## 1. 文件清单

### 1.1 新增文件（Bootloader 目录）

| 文件                      | 说明                              |
|--------------------------|----------------------------------|
| `bootloader/ota_config.h`      | OTA 配置：Flash 布局、CAN 参数、协议定义 |
| `bootloader/can_driver.h`      | CAN 底层驱动头文件                   |
| `bootloader/can_driver.c`      | CAN 底层驱动实现（轮询模式收发）        |
| `bootloader/flash_ops.h`       | Flash 操作头文件                    |
| `bootloader/flash_ops.c`       | Flash 操作实现（擦除、写入、回读验证）   |
| `bootloader/can_ota_protocol.h`| OTA 协议层头文件                    |
| `bootloader/can_ota_protocol.c`| OTA 协议层实现（帧解析、命令处理）      |
| `bootloader/bootloader.h`      | Bootloader 主逻辑头文件             |
| `bootloader/bootloader.c`      | Bootloader 主逻辑实现               |

### 1.2 新增文件（APP 目录）

| 文件                         | 说明                    |
|-----------------------------|------------------------|
| `mdk_user/Inc/ota_trigger.h`  | APP 端 OTA 触发头文件    |
| `mdk_user/Src/ota_trigger.c`  | APP 端 OTA 触发实现      |

### 1.3 新增文档

| 文件                                | 说明            |
|------------------------------------|----------------|
| `docs/can-ota-design.md`           | 技术设计文档（中文）|
| `docs/can-ota-design.en.md`        | 技术设计文档（英文）|

---

## 2. 现有文件修改

### 2.1 `main.c` 修改

在 APP 工程的 `main.c` 中需要添加 OTA 触发检查。

**修改位置**: `mdk_user/Src/main.c`

**修改内容**: 在 `main()` 函数的 `system_clock_config()` 之后添加：

```c
#include "ota_trigger.h"

int main(void)
{
  system_clock_config();
  nvic_priority_group_config(NVIC_PRIORITY_GROUP_4);

  /* ===== 新增：OTA 触发检查 ===== */
  /* 可选：在某些条件下触发 OTA（如特定 CAN 命令、按键等） */
  /* 实际调用在 CAN 命令处理回调中，此处仅作说明 */
  /* ================================ */

  /* 你的应用初始化代码 */
  /* ... */

  while(1)
  {
    /* 你的应用主循环 */
    /* ... */

    /* 在 CAN 接收回调中处理 OTA 命令 */
    /* 收到 OTA 升级命令时调用: ota_trigger_request(); */
  }
}
```

### 2.2 `at32f422_426_int.c` 修改

**修改位置**: `mdk_user/Src/at32f422_426_int.c`

**修改内容**: 填写 CAN 中断处理函数。

```c
/* includes 中添加 */
#include "ota_trigger.h"

/**
  *  @brief  can1 interrupt function rx
  *  @param  none
  *  @retval none
  */
void CAN1_RX_IRQHandler(void)
{
  can_rxbuf_type rxbuf;

  /* check if receive interrupt flag is set */
  if (can_interrupt_flag_get(CAN1, CAN_RIF_FLAG) == SET)
  {
    /* clear interrupt flag */
    can_flag_clear(CAN1, CAN_RIF_FLAG);

    /* read received frame */
    if (can_rxbuf_read(CAN1, &rxbuf) == SUCCESS)
    {
      /* ===== 在此处处理接收到的 CAN 帧 ===== */
      /* 示例：检查是否为 OTA 升级命令 */
      /*
      if (rxbuf.id == 0x100 && rxbuf.data[0] == 0x01)
      {
        // OTA upgrade command received
        ota_trigger_request();  // will reset MCU
      }
      */

      /* 你的 CAN 接收处理逻辑 */
      /* ... */

      /* release rx buffer */
      can_rxbuf_release(CAN1);
    }
  }
}

/**
  *  @brief  can1 interrupt function error
  *  @param  none
  *  @retval none
  */
void CAN1_ERR_IRQHandler(void)
{
  /* clear all error flags */
  can_flag_clear(CAN1, CAN_ALL_FLAG);

  /* your error handling logic */
  /* ... */
}
```

---

## 3. Keil 工程配置

### 3.1 Bootloader 工程（独立工程）

需要为 Bootloader 创建独立的 Keil 工程：

1. **新建工程**: `mdk_project/qi_wireless_bootloader.uvprojx`
2. **Target 配置**:
   - IROM: Start=0x08000000, Size=0x4000 (16KB)
   - IRAM: 保持默认 (0x20000000, 0x5000)
3. **添加源文件**:
   - `bootloader/bootloader.c`
   - `bootloader/can_driver.c`
   - `bootloader/flash_ops.c`
   - `bootloader/can_ota_protocol.c`
   - `mdk_user/Src/at32f422_426_clock.c`
   - `mdk_user/Src/at32f422_426_int.c`
   - `libraries/drivers/src/at32f422_426_can.c`
   - `libraries/drivers/src/at32f422_426_flash.c`
   - `libraries/drivers/src/at32f422_426_gpio.c`
   - `libraries/drivers/src/at32f422_426_crm.c`
   - `libraries/drivers/src/at32f422_426_misc.c`
   - `libraries/cmsis/device_support/system_at32f422_426.c`
4. **添加头文件路径**:
   - `bootloader/`
   - `mdk_user/Inc/`
   - `libraries/drivers/inc/`
   - `libraries/cmsis/device_support/`
   - `libraries/cmsis/core_support/`
5. **预处理宏定义**:
   - `AT32F426xx, USE_STDPERIPH_DRIVER`
6. **Linker 配置**:
   - Scatter file 中限制 code 区域到 0x08000000-0x08003FFF

### 3.2 APP 工程修改

修改现有 APP 工程：

1. **Target 配置**:
   - IROM: Start=0x08004000, Size=0xC000 (48KB)
2. **添加源文件**:
   - `mdk_user/Src/ota_trigger.c`
3. **添加头文件路径** (如未包含):
   - `mdk_user/Inc/`
4. **在 Linker 中设置偏移**:
   - Options → Linker → Use Memory Layout from Target: 不勾选
   - 或使用 scatter file 设置 ROM 起始为 0x08004000
5. **在 C/C++ 中添加宏定义**:
   - `VECT_TAB_SRAM` (可选，如果 APP 使用中断)
   - 或在 APP 启动时设置 `SCB->VTOR = APP_A_START_ADDR;`

### 3.3 Scatter File 示例（Bootloader）

```
LR_IROM1 0x08000000 0x00004000  {
  ER_IROM1 0x08000000 0x00004000  {
   *.o (RESET, +First)
   *(InRoot$$Sections)
   .ANY (+RO)
   .ANY (+XO)
  }
  RW_IRAM1 0x20000000 0x00005000  {
   .ANY (+RW +ZI)
  }
}
```

### 3.4 Scatter File 示例（APP）

```
LR_IROM1 0x08004000 0x0000C000  {
  ER_IROM1 0x08004000 0x0000C000  {
   *.o (RESET, +First)
   *(InRoot$$Sections)
   .ANY (+RO)
   .ANY (+XO)
  }
  RW_IRAM1 0x20000000 0x00005000  {
   .ANY (+RW +ZI)
  }
}
```

---

## 4. 编译顺序

1. 先编译 Bootloader 工程 → 生成 `qi_wireless_bootloader.hex`
2. 再编译 APP 工程 → 生成 `qi_wireless.hex`
3. 烧录时：先烧录 Bootloader，再烧录 APP
4. 或合并两个 hex 文件后一次烧录

---

## 5. 测试验证步骤

### 5.1 基本功能测试

1. 仅烧录 Bootloader，验证 10 秒超时后行为正确
2. 烧录 Bootloader + APP，验证正常启动跳转
3. 通过 CAN 发送 QUERY 命令，验证 Bootloader 响应
4. 在 APP 中调用 `ota_trigger_request()`，验证复位进入 OTA 模式

### 5.2 OTA 升级测试

1. 准备测试固件（修改 APP 中的版本号或 LED 闪烁频率）
2. 使用 CAN 分析仪或上位机发送 START 命令
3. 逐帧发送数据，验证每帧 ACK
4. 发送 VERIFY 命令，验证 CRC32 校验
5. 验证设备跳转到新 APP 正常运行

### 5.3 异常测试

1. 发送 CRC 错误的数据帧，验证 NAK 响应
2. 发送序列号错误的数据帧，验证 NAK 响应
3. 发送过大固件大小，验证长度错误处理
4. 中途发送 ABORT，验证会话终止
5. 超时不发送数据，验证超时跳转
