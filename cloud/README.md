# ☁️ 智能门铃云函数（后端）

> 阿里云函数计算（FC）后端服务，为智能门铃系统提供设备上传、用户认证、照片管理等 API 接口。

## 📖 项目简介

本项目是智能门铃系统的**云端核心**，基于阿里云函数计算（FC）实现，提供以下核心能力：

- **设备上传**：接收 ESP32-S3 设备端上传的图片（Base64 编码），解码后写入 OSS
- **用户认证**：用户注册、登录，账号信息存储于 Tablestore
- **照片管理**：按设备 ID 读取 OSS 索引文件，生成临时签名 URL
- **索引维护**：自动维护设备照片索引 JSON 文件，支持按设备隔离

> 🔒 安全设计：OSS 密钥存储在 FC 环境变量中，固件端无任何敏感信息。

## 🎯 功能特性

| 功能 | 接口 | 说明 |
|------|------|------|
| **设备上传** | `POST /upload` | 接收 Base64 图片，解码后写入 OSS，自动更新索引 |
| **用户注册** | `POST /register` | 邮箱 + 密码 + 设备 ID 注册（Tablestore 存储） |
| **用户登录** | `POST /login` | 邮箱 + 密码登录，返回用户信息及绑定的设备 ID |
| **获取照片** | `GET /get-photos` | 根据用户邮箱查询设备 ID，返回照片列表及签名 URL |

## 🏗️ 技术栈

| 技术 | 用途 |
|------|------|
| **Node.js 18** | 运行时环境 |
| **阿里云函数计算（FC）** | 云函数部署平台 |
| **阿里云 OSS** | 图片存储 + 索引 JSON 文件存储 |
| **阿里云 Tablestore** | 用户信息存储（邮箱 → 设备 ID 映射） |
| **ali-oss SDK** | OSS 操作（上传、读取、签名 URL） |
| **tablestore SDK** | Tablestore 操作（用户注册/登录查询） |

## 📂 项目结构

```text
cloud/
├── index.js          # 云函数主代码
├── package.json      # 依赖管理
└── README.md         # 本文档
```

## 🚀 部署指南

### 前置准备

1. **阿里云账号**，已开通函数计算（FC）、OSS、Tablestore 服务

2. **OSS Bucket**：已创建存储桶（如 smart-doorbell-photos）

3. **Tablestore 实例**：已创建实例（如 doorbell-data）和数据表 users

4. **AccessKey**：已获取 OTS_ACCESS_KEY_ID 和 OTS_ACCESS_KEY_SECRET

### 部署步骤

1. **配置环境变量**
在阿里云 FC 控制台，为函数添加以下环境变量：

|环境变量|	说明|
|-------|----|
|OTS_ACCESS_KEY_ID|	阿里云 AccessKey ID|
|OTS_ACCESS_KEY_SECRET|	阿里云 AccessKey Secret|

2. **部署函数代码**

```bash
# 进入 cloud 目录
cd cloud

# 安装依赖
npm install

# 将 index.js 和 package.json 上传到 FC 控制台
# 或在本地使用 fun/fc 工具部署
```

3. **配置 HTTP 触发器**

| 配置项	| 值 |
|------|---|
| 触发路径	| / |
|请求方法	| GET、POST、OPTIONS |
| 允许 HTTP	| 开启（或使用 HTTPS） |

## 📋 API 接口文档

### POST /upload

设备端上传图片。

- **请求头**

| 头字段	| 说明 |
|-------|-----|
| Content-Type	| application/json |

- **请求体**

```json
{
  "deviceId": "94a990dbbad0",
  "type": "pass",
  "image": "base64_encoded_jpeg_data"
}
```

- **响应（成功）**

```json
{
  "success": true,
  "objectKey": "94a990dbbad0/2026-09-09_16-41-36.jpg",
  "filename": "2026-09-09_16-41-36.jpg",
  "deviceId": "94a990dbbad0",
  "type": "pass",
  "indexUpdated": true,
  "indexError": null
}
```

### POST /register

用户注册。

- **请求体**

```json
{
  "email": "user@example.com",
  "password": "123456",
  "deviceId": "94a990dbbad0"
}
```

- **响应（成功）**

```json
{
  "success": true,
  "message": "注册成功"
}
```

### POST /login

用户登录。

- **请求体**

```json
{
  "email": "user@example.com",
  "password": "123456"
}
```

- **响应（成功）**

```json
{
  "success": true,
  "user": {
    "email": "user@example.com",
    "deviceId": "94a990dbbad0"
  }
}
```

### GET /get-photos

获取照片列表。

- **请求头**

| 头字段	| 说明 |
|-------|------|
| X-User-Email	| 用户邮箱（用于查询绑定的设备 ID） |

- **响应（成功）**

```json
[
  {
    "filename": "2026-09-09_16-41-36.jpg",
    "type": "pass",
    "timestamp": 1788523296000,
    "url": "https://bucket.oss-cn-guangzhou.aliyuncs.com/...?Expires=...&Signature=...",
    "deviceId": "94a990dbbad0"
  }
]
```

## 🔐 安全设计

| 安全措施	| 说明 |
|--------|------|
|密钥环境变量化	| OSS 密钥存储在 FC 环境变量中，固件无任何 AK/SK|
|设备 ID 隔离	| OSS 路径按设备 ID 隔离，不同设备数据互不可见|
|签名 URL	| 图片通过临时签名 URL（有效期 5 分钟）访问，OSS 保持私有读权限|
|CORS 配置	| 允许跨域请求，支持网页端直接调用|


## 🔧 关键修复记录

### 索引更新 ENAMETOOLONG 错误

- **问题**：使用 ossClient.put(indexKey, JSON.stringify(photos)) 时，OSS SDK 将字符串误判为本地文件路径，导致 fs.stat 抛出 ENAMETOOLONG。

- **修复**：

```javascript
// ❌ 错误写法
await ossClient.put(indexKey, JSON.stringify(photos));

// ✅ 正确写法
await ossClient.put(indexKey, Buffer.from(JSON.stringify(photos)), {
    headers: { 'Content-Type': 'application/json' }
});
```

- **原因**：OSS SDK 的 put 方法第二个参数为字符串时会识别为本地文件路径，传入 Buffer 时识别为文件内容。

## 📄 License
Copyright (c) 2026 林佳佳

本作品为毕业设计项目，仅供展示和学习参考。
未经作者明确书面许可，不得复制、修改、分发或用于商业用途。

## 🔗 相关项目

- [设备端固件](../firmware/) - ESP32-S3 门铃固件
- [前端照片墙](../frontend/) - GitHub Pages 部署的 Web 应用
- [硬件设计](../hardware/) - 硬件外设清单与接线图
