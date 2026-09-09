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

<img width="2239" height="929" alt="系统架构图" src="https://github.com/user-attachments/assets/8a066af8-64e6-4be7-a90a-ac505d4b8e2f" />



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

5. **配置 FC 上传地址**

修改 `main/cloud_upload.c`，填入阿里云 FC 的 HTTP 触发 URL：

```c
#define FC_UPLOAD_URL "https://your-fc-domain/upload"
```

> 注意：本固件不包含 OSS 密钥。图片通过 FC 云函数代理上传，密钥存储在 FC 的环境变量中，固件仅包含 FC 的公开 URL，无任何敏感信息。

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

固件将 JPEG 图片 Base64 编码后，通过 JSON 格式 POST 到阿里云 FC（函数计算），由 FC 代理完成：

- **上传到 OSS**：FC 解码 Base64 并写入 OSS
- **索引更新**：FC 自动维护 `{deviceId}/{deviceId}.json` 索引文件
- **密钥安全**：OSS 密钥存储在 FC 环境变量中，固件无任何敏感信息

**数据格式**：
- 请求方式：`POST /upload`
- Content-Type：`application/json`
- 请求体：

```json
{
  "deviceId": "94a990dbbad0",
  "type": "pass",
  "image": "base64_encoded_jpeg"
}
```

**路径格式**:

- 图片：`{deviceId}/{filename}.jpg`

- 索引文件：`{deviceId}/{deviceId}.json`
  
- 每条记录包含：`filename、type（pass/stay）、timestamp、deviceId`

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

| 安全措施 | 说明 |
|----------|------|
| **FC 代理上传** | OSS 密钥存储在 FC 环境变量中，固件无任何 AK/SK，物理提取固件也无法获取云权限 |
| **设备 ID 隔离** | OSS 路径按设备 ID 区分，不同设备数据隔离 |
| **OSS 私有读** | 图片不公开访问，需 FC 生成签名 URL（有效期 300 秒） |
| **配网动态密码** | 每设备随机生成 6 位密码，防止未授权配网 |
| **NVS 持久化** | WiFi 配网信息存储于 NVS，断电不丢失 |
| **AP+STA 共存** | AP 常驻，即使 STA 断网也可通过热点配网恢复 |
| **HTTPS 支持** | 可启用 HTTPS（需配置证书） |

## 🧪 运行日志示例

```text
I (1333) MAIN: ==========================================
I (1343) MAIN: 智能门铃系统启动中...
I (1343) MAIN: ==========================================
I (1383) MAIN: ✅ NVS初始化完成
I (1383) 设备ID: 从NVS加载设备ID: 94a990dbbad0
I (1383) MAIN: ✅ 设备ID初始化完成
I (1413) GPIO: GPIO初始化完成: PIR=GPIO18 (下拉), 内置按键=GPIO0, 外接按键=GPIO39
I (1423) MAIN: ✅ GPIO初始化完成
I (1433) PWM: 蜂鸣器已初始化 (GPIO40)
I (1443) MAIN: ✅ 蜂鸣器初始化完成
I (1443) WIFI: 初始化WiFi (AP+STA模式)...
I (1573) NVS: AP配置已加载: SSID=Doorbell_Config
I (1573) WIFI: 加载自定义AP配置: SSID=Doorbell_Config
I (1693) wifi:mode : sta (94:a9:90:db:ba:d0) + softAP (94:a9:90:db:ba:d1)
I (1703) WIFI: STA 启动，开始连接...
I (1723) NVS: WiFi配置已加载: SSID=cptbtptp
I (1743) WIFI: 发现保存的WiFi配置，立即连接...
I (1773) WIFI: 正在连接WiFi: cptbtptp
I (1773) WIFI: ✅ WiFi初始化完成，AP SSID: Doorbell_Config
I (1773) MAIN: ✅ WiFi管理器初始化完成
W (1823) WIFI: WiFi断开，重试第 1 次...
I (1893) wifi:connected with cptbtptp, aid = 2, channel 10, BW20
I (3473) esp_netif_handlers: sta ip: 192.168.43.49, mask: 255.255.255.0, gw: 192.168.43.1
I (3473) WIFI: ✅ 获取到IP地址: 192.168.43.49
I (3473) WIFI: 手动设置 DNS: 8.8.8.8, 114.114.114.114
I (3553) MAIN: ✅ STA已连接，IP: 192.168.43.49
W (13553) MAIN: ⚠️ SNTP同步超时，时间可能不准确
I (13553) CAMERA: 正在初始化摄像头...
I (13793) CAMERA: ✅ 真实摄像头初始化成功！
I (13793) CAMERA:    分辨率: QVGA (320x240)
I (13793) CAMERA:    格式: JPEG
I (13803) MAIN: ✅ 摄像头初始化完成
I (13803) MAIN: 正在加载行人检测模型...
I (14003) MAIN: ✅ 行人检测器创建成功 (阈值=0.3)
I (14003) MAIN: 🔔 门铃按下！播放旋律...
I (14103) MAIN: PIR检测任务已启动 (使用esp-dl行人检测)
I (14113) WEB: 🔑 随机密码: 669046
I (14123) WEB: ✅ Web服务器已启动，请访问 http://192.168.4.1 并输入密码
I (14133) MAIN: ✅ 所有系统就绪。

--- 首次 PIR 触发（时间未同步，AI 未检测到人形）---
I (15753) MAIN: 🔴 PIR触发！ (时间: 未同步)
I (15753) CAMERA: 📷 真实拍照成功: 1970-01-01_08-00-14.jpg
I (16673) MAIN: 第一次抓拍未检测到人，丢弃

--- 第二次 PIR 触发（时间已同步，检测到人形，上传 pass）---
I (194073) MAIN: 🔴 PIR触发！ (时间: 16:41:36)
I (194073) CAMERA: 📷 真实拍照成功: 2026-09-09_16-41-36.jpg
I (195023) MAIN: 检测框: x=251, y=0, w=182, h=346, 置信度=0.815
I (195323) 云存储: 正在上传图片到 FC，原始大小 27995 字节，类型 pass
I (197463) 云存储: HTTP 状态码: 200
I (197473) 云存储: ✅ 上传成功，OSS 路径: 未知
I (197483) MAIN: ✅ 第一张照片上传成功并更新索引 (pass)

--- 5秒后二次确认，人员仍在 → 上传 stay ---
I (197493) MAIN: ⏳ 等待5秒进行二次确认...
I (202493) CAMERA: 📷 真实拍照成功: 2026-09-09_16-41-44.jpg
I (203443) MAIN: 确认框: x=348, y=1, w=226, h=371
I (203733) 云存储: 正在上传图片到 FC，原始大小 26389 字节，类型 stay
I (204633) 云存储: HTTP 状态码: 200
I (204643) 云存储: ✅ 上传成功，OSS 路径: 未知
W (204653) MAIN: ⚠️ 5秒后人员仍然存在！上传为STAY并更新索引。

--- 第三次 PIR 触发（检测到人形，上传 pass，5秒后离开 → 仅路过）---
I (207653) MAIN: 🔴 PIR触发！ (时间: 16:41:49)
I (207653) CAMERA: 📷 真实拍照成功: 2026-09-09_16-41-49.jpg
I (208593) MAIN: 检测框: x=219, y=0, w=156, h=249, 置信度=0.679
I (208893) 云存储: 正在上传图片到 FC，原始大小 25840 字节，类型 pass
I (210303) 云存储: HTTP 状态码: 200
I (210313) 云存储: ✅ 上传成功，OSS 路径: 未知
I (210323) MAIN: ✅ 第一张照片上传成功并更新索引 (pass)
I (210333) MAIN: ⏳ 等待5秒进行二次确认...
I (215333) CAMERA: 📷 真实拍照成功: 2026-09-09_16-41-57.jpg
I (216273) MAIN: 确认框: x=109, y=1, w=182, h=223
I (216573) 云存储: 正在上传图片到 FC，原始大小 25197 字节，类型 stay
I (217503) 云存储: HTTP 状态码: 200
I (217513) 云存储: ✅ 上传成功，OSS 路径: 未知
I (217523) MAIN: ⚠️ 5秒后人员仍然存在！上传为STAY并更新索引。

--- 第四次 PIR 触发（未检测到人形，丢弃）---
I (222723) MAIN: 🔴 PIR触发！ (时间: 16:42:04)
I (222723) CAMERA: 📷 真实拍照成功: 2026-09-09_16-42-04.jpg
I (223653) MAIN: 第一次抓拍未检测到人，丢弃

--- 第五次 PIR 触发（检测到人形，5秒后离开 → 仅路过）---
I (235503) MAIN: 🔴 PIR触发！ (时间: 16:42:17)
I (235503) CAMERA: 📷 真实拍照成功: 2026-09-09_16-42-17.jpg
I (236453) MAIN: 检测框: x=555, y=0, w=244, h=595, 置信度=0.538
I (236753) 云存储: 正在上传图片到 FC，原始大小 27796 字节，类型 pass
I (237813) 云存储: HTTP 状态码: 200
I (237823) 云存储: ✅ 上传成功，OSS 路径: 未知
I (237833) MAIN: ✅ 第一张照片上传成功并更新索引 (pass)
I (237833) MAIN: ⏳ 等待5秒进行二次确认...
I (242843) CAMERA: 📷 真实拍照成功: 2026-09-09_16-42-25.jpg
I (243763) MAIN: 人员在5秒内离开，仅路过。

--- 门铃按键触发 ---
I (248243) MAIN: 🔔 门铃按下！播放旋律...
I (253343) PWM: 旋律播放结束
```

## 📄 License

MIT © 林佳佳

详见 [LICENSE](../LICENSE) 文件。

## 🔗 相关项目

- [前端照片墙](../frontend/) - GitHub Pages 部署的 Web 应用
- [云函数后端](../cloud/) - 阿里云 FC 云函数
- [硬件设计](../hardware/) - 硬件外设清单与接线图
