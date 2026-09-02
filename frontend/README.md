# 📸 智能门铃照片墙（前端）

> 智能门铃系统的用户端 Web 应用，支持用户注册/登录，远程查看门铃抓拍的照片记录。

## 📖 项目简介

本项目是智能门铃系统的**前端展示层**，提供以下核心能力：

- 用户注册与登录（账号信息存储于阿里云 Tablestore）
- 照片列表展示（按设备 ID 自动过滤）
- 照片按日期/类型筛选（路过/可疑停留）
- 大图预览与保存下载

> 注：本前端为纯静态 HTML/CSS/JS 应用，通过 HTTPS 调用阿里云 FC 云函数 API 获取数据。

## 🎯 功能特性

| 功能 | 说明 |
|------|------|
| **用户注册** | 邮箱 + 密码 + 设备 ID 注册，密码至少 6 位 |
| **用户登录** | 邮箱 + 密码登录，登录态保存在 sessionStorage |
| **照片列表** | 按设备 ID 自动过滤，按时间倒序排列 |
| **日期筛选** | 选择日期查看当天照片 |
| **类型筛选** | 全部 / 仅路过 (pass) / 仅可疑停留 (stay) |
| **大图预览** | 点击照片弹出模态框查看大图 |
| **保存图片** | 在大图预览中可将图片保存到本地 |
| **退出登录** | 清除登录态，返回登录页 |

## 🏗️ 技术栈

| 技术 | 用途 |
|------|------|
| **HTML5** | 页面结构 |
| **CSS3** | 界面样式（暗色主题，移动端优先） |
| **JavaScript (ES6+)** | 业务逻辑、API 调用 |
| **阿里云 FC** | 云函数后端 API（登录/注册/获取照片） |
| **阿里云 OSS** | 照片存储（通过签名 URL 访问） |
| **GitHub Pages** | 前端托管（也可部署到任何静态服务器） |

```text
## 📂 项目结构
frontend/
├── index.html # 主页面（包含全部 CSS 和 JavaScript）
└── README.md # 项目说明文档
```

## 🚀 部署指南

### 方式一：GitHub Pages（推荐）

1. 将 `index.html` 推送到你的 GitHub 仓库
2. 进入仓库 **Settings** → **Pages**
3. **Source** 选择 `Deploy from a branch`
4. **Branch** 选择 `main`，文件夹选择 `/frontend`
5. 点击 **Save**

部署完成后，访问 `https://你的用户名.github.io/仓库名/frontend/` 即可。

### 方式二：任意静态服务器

直接将 `index.html` 部署到任何静态服务器（如 Nginx、Vercel、Netlify、OSS 静态托管等）。

## ⚙️ 配置说明

### 修改 API 地址

打开 `index.html`，找到以下代码行并修改为你的 FC 云函数公网地址：

```javascript
const API_BASE = 'https://doorbell-api-jclvfiemao.cn-shenzhen.fcapp.run';
```
### 后端 API 依赖
本前端需要以下后端接口支持：

|接口	|方法|	说明|
|------|------|------|
|/register|	POST	|用户注册|
|/login	|POST	|用户登录|
|/get-photos	|GET|	获取照片列表（需携带 X-User-Email 头）|
后端实现详见项目根目录的 cloud/ 文件夹。

## 🔐 安全说明

- **登录态**：登录成功后，用户信息存储在 `sessionStorage` 中，关闭浏览器标签页即清除
- **设备 ID**：注册时绑定，登录后自动关联，用户只能查看自己设备 ID 对应的照片
- **图片访问**：所有图片通过 FC 云函数生成的**临时签名 URL** 访问，有效期 5 分钟，防止 OSS 数据被公开访问

## 📱 界面预览
|登录页|	照片墙|	大图预览|
|------|------|------|
|https://via.placeholder.com/200x400?text=Login|	https://via.placeholder.com/200x400?text=Photos|	https://via.placeholder.com/200x400?text=Preview
实际效果请访问部署后的在线地址。

## 📄 License
MIT © 林佳佳

## 🔗 相关项目

- [设备端固件](../firmware/) - ESP32-S3 门铃固件
- [云函数后端](../cloud/) - 阿里云 FC 云函数
- [硬件设计](../hardware/) - 硬件外设清单与接线图
