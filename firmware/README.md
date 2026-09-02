# 🔔 ESP32-S3 智能门铃固件

> 端侧智能门铃固件，基于 ESP32-S3 平台，集成边缘 AI 行人检测、PIR 人体感应、摄像头抓拍与云端上传。

## 📖 项目简介

本固件是智能门铃系统的**设备端核心**，运行于 ESP32-S3 开发板上，实现以下功能：

- **人体感应**：通过 PIR 传感器检测人体移动，低功耗唤醒
- **图像采集**：驱动 OV2640 摄像头抓拍 JPEG 图像
- **边缘 AI 推理**：基于 esp-dl 推理框架，片上运行行人检测模型
- **云端上传**：通过 HTTP PUT 直传图片到阿里云 OSS，路径按设备 ID 隔离
- **WiFi 配网**：支持 SoftAP 配网模式，配网信息 NVS 持久化存储
- **本地 Web 服务器**：提供配网页面，显示设备 ID 与随机密码
- **门铃按键**：外接按键触发蜂鸣器播放《小星星》旋律

## 🎯 技术亮点

| 技术点 | 说明 |
|--------|------|
| **端侧 AI** | 使用 esp-dl 推理框架，加载行人检测模型，片上推理 |
| **路过/逗留区分** | 首次抓拍上传（pass），5 秒后二次确认（stay） |
| **低误报设计** | PIR 多级采样滤波（10 次采样取高电平占比）+ 12 秒冷却 |
| **异常降级** | 摄像头初始化失败自动切换模拟 JPEG 模式 |
| **设备 ID 体系** | 从 MAC 地址生成并持久化到 NVS，一机一码 |
| **多任务 RTOS** | FreeRTOS 任务分离：PIR 检测、按键响应、Web 服务器 |
| **安全配网** | SoftAP 配网 + 6 位动态随机密码（esp_random） |
| **资源保护** | 互斥锁保护共享照片缓冲区，避免任务间竞争 |

## 🏗️ 系统架构

<img width="817" height="1047" alt="image" src="https://github.com/user-attachments/assets/0fd8bfb6-91b0-4982-97ba-ebb8662eb736" />


## 📂 项目结构
```text
firmware/
├── main/
│ ├── main.cpp # 主入口，任务创建与初始化
│ ├── camera_driver.c/h # 摄像头驱动（OV2640）
│ ├── gpio_control.c/h # GPIO 控制（PIR、按键）
│ ├── wifi_manager.c/h # WiFi 管理（STA/SoftAP）
│ ├── web_server.c/h # 本地 Web 配网服务器
│ ├── cloud_upload.c/h # 阿里云 OSS 上传
│ ├── pwm_control.c/h # 蜂鸣器 PWM 驱动
│ ├── ai_detection.c/h # AI 推理接口（esp-dl）
│ ├── device_id.c/h # 设备 ID 生成与管理
│ └── nvs_storage.c/h # NVS 配置持久化
├── components/ # 外部组件
│ ├── esp-dl/ # ESP 深度学习框架
│ ├── esp_jpeg/ # JPEG 解码库
│ └── ...
├── managed_components/ # 组件管理器下载的依赖
├── CMakeLists.txt # 项目构建文件
├── partitions.csv # 分区表配置
├── idf_component.yml # 组件依赖声明
└── README.md # 本文档
```

## 🔧 编译与烧录

### 环境准备

1. **安装 ESP-IDF 开发环境**
   - 参考 [ESP-IDF 官方文档](https://docs.espressif.com/projects/esp-idf/zh_CN/latest/esp32s3/get-started/)
   - 推荐版本：v5.4.4 或以上

2. **克隆代码并进入目录**

```bash
git clone https://github.com/LJJ26liu/smart-doorbell.git
cd smart-doorbell/firmware
```
3. **配置目标芯片**

```bash
idf.py set-target esp32s3
```

4. **配置 Flash 大小**

```bash
idf.py menuconfig
# 进入 Serial flasher config → Flash size → 选择 8 MB（根据实际开发板调整）
```

5. **配置 OSS 密钥**

修改 main/cloud_upload.h，填入阿里云 OSS 密钥：

```c
#define OSS_ACCESS_KEY     "YOUR_ACCESS_KEY"
#define OSS_ACCESS_SECRET  "YOUR_ACCESS_SECRET"
#define OSS_ENDPOINT       "oss-cn-guangzhou.aliyuncs.com"
#define OSS_BUCKET         "your-bucket-name"
```

### 编译
```bash
idf.py build
```
### 烧录
```bash
idf.py -p /dev/ttyUSB0 flash
```
> （Windows 下端口为 COMx，Linux/macOS 为 /dev/ttyUSBx）

### 查看日志
```bash
idf.py monitor
```
## 📋 硬件接线说明

|外设	|引脚|	说明|
|-----|---|------|
|OV2640 摄像头|	GPIO3-10, 11, 12, 13, 14, 15, 16, 17, 21	|详见 camera_driver.h|
|PIR 传感器|	GPIO18	|输入，内部下拉，高电平触发|
|门铃按键（外接）|	GPIO39	|输入，内部上拉，按下低电平|
|蜂鸣器|	GPIO40|	PWM 输出，低电平鸣叫|
|BOOT 按键|	GPIO0	|内部上拉，按下低电平（配网重置可用）|

> 详细接线图请参考项目根目录的 hardware/ 文件夹。

## ⚙️ 功能说明
### 1. **PIR 人体感应**
- **10 次采样滤波，消除误触**

- **12 秒冷却机制，防止重复触发**

- **高电平有效，检测到人时触发抓拍流程**

### 2. **边缘 AI 推理**
- **使用 pedestrian_detect_pico_s8_v1.espdl 模型（esp-dl）**

- **检测框过滤：宽高比 0.25~0.9、面积占比 0.5%~80%、位置过滤**

- **置信度阈值：0.3**

### 3. **路过/逗留区分**

|阶段	|操作|	标记|
|----|----|----|
|首次检测|	立即抓拍 + AI 推理 + 上传	|pass|
|5 秒后二次确认|	重新抓拍 + 推理，若仍有人形	|stay|
|人员离开|	仅上传第一张（标记 pass）	| - |

### 4. **云端上传**

- **路径格式**：{deviceId}/{filename}.jpg

- **索引文件**：{deviceId}/{deviceId}.json

- **每条记录包含**：filename、type（pass/stay）、timestamp、deviceId

### 5. WiFi 配网

固件支持两种网络模式，按优先级自动切换：

| 模式 | 触发条件 | 说明 |
|------|----------|------|
| **STA 模式** | NVS 中已保存有效 WiFi 配置 | 自动连接已配置的 STA WiFi，连接成功后设备进入正常工作状态 |
| **SoftAP 模式** | ① 首次启动无配置<br>② 已保存的 WiFi 连接失败或超时 | 设备作为热点启动，用户可通过手机/电脑连接，进入配网页面设置 WiFi |

#### STA 模式（正常工作模式）

- 固件启动时自动读取 NVS 中保存的 WiFi 配置（SSID / Password）
- 自动连接，连接成功后获取 IP 地址（串口日志可查看）
- 若连接失败或超时，自动切换至 SoftAP 模式

#### SoftAP 配网模式

- **热点 SSID**：`Doorbell_Config`（可自定义，修改 `nvs_storage.c` 中的默认值）
- **热点密码**：`12345678`（可自定义，长度 ≥ 8 位）
- **配网页面**：连接热点后，浏览器访问 `http://192.168.4.1`
- **页面登录密码**：6 位随机数，**仅通过串口日志输出**（每次启动随机生成，见串口输出 `🔑 随机密码: XXXXXX`）
- **配网流程**：输入随机密码登录 → 填写目标 WiFi SSID 和密码 → 保存 → 设备重启，自动切换至 STA 模式

#### 注意事项

- 配网页面支持显示当前设备 ID，方便用户在云端注册时关联
- 配网信息通过 NVS 持久化存储，断电后不丢失
- 如需重置配网，可通过长按 BOOT 按键（GPIO0）3 秒（代码已预留逻辑，可根据需要启用）

### 6. **本地 Web 服务器**

- **端口**：80

- **功能**：WiFi 配网设置、设备 ID 显示

- **登录密码**：每设备唯一，串口日志输出

## 🔐 安全设计

|安全措施	|说明|
|---|---|
|设备 ID 隔离	|OSS 路径按设备 ID 区分，不同设备数据隔离|
|OSS 私有读	|图片不公开访问，需签名 URL|
|配网动态密码	|每设备随机生成 6 位密码，防止未授权配网|
|NVS 持久化	|WiFi 配网信息加密存储于 NVS|
|HTTPS 支持	|可启用 HTTPS（需证书）|

## 🧪 运行日志示例

```text
I (1234) MAIN: ==========================================
I (1234) MAIN: 智能门铃系统启动中...
I (1234) MAIN: ==========================================
I (1234) NVS: NVS初始化完成
I (1234) 设备ID: 设备ID已加载: 94a990dbbad0
I (1234) GPIO: GPIO初始化完成: PIR=GPIO18, 内置按键=GPIO0, 外接按键=GPIO39
I (1234) PWM: 蜂鸣器已初始化 (GPIO40)
I (1234) WIFI: WiFi初始化完成
I (1234) WIFI: 正在连接已保存的WiFi: MyHomeWiFi
I (1234) WIFI: WiFi事件 ID: 4
I (1234) WIFI: STA模式启动，正在连接WiFi...
I (1234) WIFI: 获取到IP地址: 192.168.1.100
I (1234) SNTP: ✅ SNTP时间同步成功: Mon Sep  2 10:30:00 2026
I (1234) CAMERA: 正在初始化摄像头...
I (1234) CAMERA: ✅ 真实摄像头初始化成功！
I (1234) MAIN: 正在加载行人检测模型...
I (1234) MAIN: ✅ 行人检测器创建成功 (阈值=0.3)
I (1234) WEB: ========================================
I (1234) WEB: 🔑 随机密码: 723641
I (1234) WEB: ========================================
I (1234) WEB: ✅ Web服务器已启动，请访问 http://192.168.4.1 并输入密码
I (1234) MAIN: ✅ 所有系统就绪。
🔴 PIR触发！ (时间: 10:30:15)
📷 真实拍照成功: 2026-09-02_10-30-15.jpg (62341 字节)
I (1234) 云存储: 正在上传到 http://bucket.oss-cn-guangzhou.aliyuncs.com/94a990dbbad0/2026-09-02_10-30-15.jpg
I (1234) 云存储: ✅ 上传成功！ETag: "abcd1234"
✅ 第一张照片上传成功并更新索引 (pass)
⏳ 等待5秒进行二次确认...
```

## 📄 License
MIT © 林佳佳

## 🔗 相关项目

- [前端照片墙](../frontend/) - GitHub Pages 部署的 Web 应用
- [云函数后端](../cloud/) - 阿里云 FC 云函数
- [硬件设计](../hardware/) - 硬件外设清单与接线图
