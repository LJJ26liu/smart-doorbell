'use strict';

const TableStore = require('tablestore');
const OSS = require('ali-oss');

const OTS_ACCESS_KEY_ID = process.env.OTS_ACCESS_KEY_ID;
const OTS_ACCESS_KEY_SECRET = process.env.OTS_ACCESS_KEY_SECRET;

const client = new TableStore.Client({
    accessKeyId: OTS_ACCESS_KEY_ID,
    accessKeySecret: OTS_ACCESS_KEY_SECRET,
    endpoint: 'https://doorbell-data.cn-shenzhen.ots.aliyuncs.com',
    instancename: 'doorbell-data'
});

// 保留外部 ossClient 用于其他函数
const ossClient = new OSS({
    accessKeyId: OTS_ACCESS_KEY_ID,
    accessKeySecret: OTS_ACCESS_KEY_SECRET,
    region: 'oss-cn-guangzhou',
    bucket: 'smart-doorbell-photos',
});

const tableName = 'users';

const CORS_HEADERS = {
    'Content-Type': 'application/json',
    'Access-Control-Allow-Origin': '*',
    'Access-Control-Allow-Headers': 'Content-Type, Authorization, X-User-Email, X-Device-Id, X-Photo-Type',
    'Access-Control-Allow-Methods': 'GET, POST, PUT, OPTIONS',
};

// ============================================================
//  路径解析
// ============================================================
function normalizeEvent(event) {
    if (Buffer.isBuffer(event)) {
        try {
            return JSON.parse(event.toString('utf-8'));
        } catch (e) {
            return {};
        }
    }
    return event || {};
}

function getPath(evt) {
    let p = evt.rawPath || 
            evt.path || 
            (evt.requestContext && evt.requestContext.http && evt.requestContext.http.path) ||
            (evt.requestContext && evt.requestContext.path) ||
            '/';
    const idx = p.indexOf('?');
    if (idx !== -1) p = p.substring(0, idx);
    return p;
}

function getMethod(evt) {
    return evt.requestContext?.http?.method || evt.httpMethod || 'GET';
}

function getBody(evt) {
    if (!evt.body) return {};
    if (typeof evt.body === 'string') {
        try { return JSON.parse(evt.body); } catch (e) {}
    }
    return evt.body || {};
}

function getHeaders(evt) {
    return evt.headers || {};
}

// ============================================================
//  辅助函数
// ============================================================
async function verifyUser(email) {
    if (!email) return null;
    try {
        const result = await client.getRow({
            tableName,
            primaryKey: [{ 'email': email }],
            maxVersions: 1
        });
        if (!result.row || !result.row.attributes) return null;
        const deviceId = String(result.row.attributes.find(a => a.columnName === 'deviceId')?.columnValue || '').trim();
        return { email, deviceId };
    } catch (err) {
        console.error('验证用户失败:', err);
        return null;
    }
}

// ============================================================
//  接口处理
// ============================================================
async function handleRegister(body) {
    const { email, password, deviceId } = body;
    if (!email || !password || !deviceId) {
        return { statusCode: 400, body: JSON.stringify({ success: false, error: '缺少必要字段' }) };
    }
    if (password.length < 6) {
        return { statusCode: 400, body: JSON.stringify({ success: false, error: '密码至少6位' }) };
    }
    try {
        const exist = await client.getRow({ tableName, primaryKey: [{ 'email': email }], maxVersions: 1 });
        if (exist.row) {
            return { statusCode: 400, body: JSON.stringify({ success: false, error: '该邮箱已注册' }) };
        }
    } catch (err) {
        console.error('检查注册失败:', err);
        return { statusCode: 500, body: JSON.stringify({ success: false, error: '服务器错误' }) };
    }
    try {
        await client.putRow({
            tableName,
            condition: new TableStore.Condition(TableStore.RowExistenceExpectation.EXPECT_NOT_EXIST),
            primaryKey: [{ 'email': email }],
            attributeColumns: [
                { 'password': password },
                { 'deviceId': deviceId },
                { 'createdAt': new Date().toISOString() }
            ]
        });
    } catch (err) {
        console.error('注册写入失败:', err);
        return { statusCode: 500, body: JSON.stringify({ success: false, error: '注册失败，请重试' }) };
    }
    return { statusCode: 200, body: JSON.stringify({ success: true, message: '注册成功' }) };
}

async function handleLogin(body) {
    const { email, password } = body;
    if (!email || !password) {
        return { statusCode: 400, body: JSON.stringify({ success: false, error: '请输入邮箱和密码' }) };
    }
    let result;
    try {
        result = await client.getRow({ tableName, primaryKey: [{ 'email': email }], maxVersions: 1 });
    } catch (err) {
        console.error('登录查询失败:', err);
        return { statusCode: 500, body: JSON.stringify({ success: false, error: '服务器错误' }) };
    }
    if (!result.row) {
        return { statusCode: 401, body: JSON.stringify({ success: false, error: '邮箱或密码错误' }) };
    }
    const storedPassword = String(result.row.attributes.find(a => a.columnName === 'password')?.columnValue || '').trim();
    const storedDeviceId = String(result.row.attributes.find(a => a.columnName === 'deviceId')?.columnValue || '').trim();
    if (storedPassword !== String(password).trim()) {
        return { statusCode: 401, body: JSON.stringify({ success: false, error: '邮箱或密码错误' }) };
    }
    return {
        statusCode: 200,
        body: JSON.stringify({ success: true, user: { email, deviceId: storedDeviceId } })
    };
}

async function handleGetPhotos(headers) {
    const userEmail = headers['x-user-email'] || headers['X-User-Email'];
    if (!userEmail) {
        return { statusCode: 401, body: JSON.stringify({ success: false, error: '未登录' }) };
    }
    const user = await verifyUser(userEmail);
    if (!user) {
        return { statusCode: 401, body: JSON.stringify({ success: false, error: '用户不存在' }) };
    }
    const deviceId = user.deviceId;
    if (!deviceId) {
        return { statusCode: 400, body: JSON.stringify({ success: false, error: '用户未绑定设备ID' }) };
    }
    try {
        const objectKey = `${deviceId}/${deviceId}.json`;
        let photos = [];
        try {
            const result = await ossClient.get(objectKey);
            photos = JSON.parse(result.content.toString('utf-8')) || [];
        } catch (err) {
            if (err.code === 'NoSuchKey') {
                return { statusCode: 200, body: JSON.stringify([]) };
            }
            throw err;
        }
        const result = photos
            .filter(item => item.filename)
            .map(item => ({
                filename: item.filename,
                type: item.type || 'pass',
                timestamp: item.timestamp || Date.now(),
                url: ossClient.signatureUrl(`${deviceId}/${item.filename}`, { expires: 300, method: 'GET' }).replace(/^http:/, 'https:'),
                deviceId
            }));
        return { statusCode: 200, body: JSON.stringify(result) };
    } catch (err) {
        console.error('读取照片列表失败:', err);
        return { statusCode: 500, body: JSON.stringify({ success: false, error: '读取照片列表失败: ' + err.message }) };
    }
}

// ============================================================
//  handleUpload - 接收干净的 body，不依赖 event
// ============================================================
async function handleUpload(body) {
    const deviceId = body.deviceId;
    const image = body.image;
    const photoType = body.type || 'pass';

    // 校验
    if (!deviceId) {
        return { statusCode: 400, body: JSON.stringify({ success: false, error: '缺少 deviceId' }) };
    }
    if (typeof deviceId !== 'string') {
        return { statusCode: 400, body: JSON.stringify({ success: false, error: 'deviceId 类型错误' }) };
    }
    if (deviceId.startsWith('[') || deviceId.startsWith('{')) {
        return { statusCode: 400, body: JSON.stringify({ success: false, error: 'deviceId 格式错误（包含 JSON 结构）' }) };
    }
    if (!image) {
        return { statusCode: 400, body: JSON.stringify({ success: false, error: '缺少 image' }) };
    }

    let imageBuffer;
    try {
        imageBuffer = Buffer.from(image, 'base64');
    } catch (e) {
        return { statusCode: 400, body: JSON.stringify({ success: false, error: 'Base64 解码失败' }) };
    }

    const now = new Date();
    const pad = n => String(n).padStart(2, '0');
    const filename = `${now.getFullYear()}-${pad(now.getMonth()+1)}-${pad(now.getDate())}_${pad(now.getHours())}-${pad(now.getMinutes())}-${pad(now.getSeconds())}.jpg`;
    const objectKey = `${deviceId}/${filename}`;

    // 独立 OSS 客户端
    const LocalOSS = require('ali-oss');
    const localOssClient = new LocalOSS({
        accessKeyId: process.env.OTS_ACCESS_KEY_ID,
        accessKeySecret: process.env.OTS_ACCESS_KEY_SECRET,
        region: 'oss-cn-guangzhou',
        bucket: 'smart-doorbell-photos',
    });

    try {
        // 上传图片
        await localOssClient.put(objectKey, imageBuffer, { headers: { 'Content-Type': 'image/jpeg' } });
        console.log(`✅ 图片上传成功: ${objectKey}`);

        // 索引更新
        const indexKey = `${deviceId}/${deviceId}.json`;
        console.log('[索引] indexKey:', indexKey);

        if (indexKey.startsWith('[') || indexKey.startsWith('{')) {
            throw new Error(`indexKey 格式错误: ${indexKey}`);
        }

        let photos = [];
        try {
            const result = await localOssClient.get(indexKey);
            const content = result.content.toString('utf-8');
            photos = JSON.parse(content);
            if (!Array.isArray(photos)) photos = [];
            console.log(`[索引] 读取成功，现有 ${photos.length} 条记录`);
        } catch (err) {
            if (err.code === 'NoSuchKey') {
                console.log('[索引] 索引文件不存在，创建新文件');
                photos = [];
            } else {
                console.error('[索引] 读取失败:', err);
                throw err;
            }
        }

        photos = photos.filter(p => p.filename !== filename);
        photos.push({
            filename: filename,
            type: photoType,
            timestamp: Date.now(),
            deviceId: deviceId
        });

        // ✅ 关键修复：必须使用 Buffer 包装，避免 OSS SDK 将字符串误判为本地文件路径
        await localOssClient.put(indexKey, Buffer.from(JSON.stringify(photos)), {
            headers: { 'Content-Type': 'application/json' }
        });
        console.log(`✅ [索引] 成功写入 ${indexKey}，共 ${photos.length} 条记录`);

        return {
            statusCode: 200,
            body: JSON.stringify({
                success: true,
                objectKey,
                filename,
                deviceId,
                type: photoType,
                indexUpdated: true,
                indexError: null,
            })
        };
    } catch (err) {
        console.error('❌ 处理失败:', err);
        return {
            statusCode: 500,
            body: JSON.stringify({
                success: false,
                error: err.message,
            })
        };
    }
}

// ============================================================
//  主入口
// ============================================================
exports.handler = async (event, context) => {
    const evt = normalizeEvent(event);
    const path = getPath(evt);
    const method = getMethod(evt);
    const body = getBody(evt);
    const headers = getHeaders(evt);

    console.log(`路径: ${path}, 方法: ${method}`);

    if (method === 'OPTIONS') {
        return { statusCode: 200, headers: CORS_HEADERS, body: '' };
    }

    let response;
    switch (path) {
        case '/upload':
            if (method === 'POST') {
                // 直接传递已解析的 body
                response = await handleUpload(body);
            } else {
                response = { statusCode: 405, body: JSON.stringify({ success: false, error: 'Method Not Allowed' }) };
            }
            break;
        case '/login':
            if (method === 'POST') response = await handleLogin(body);
            else response = { statusCode: 405, body: JSON.stringify({ success: false, error: 'Method Not Allowed' }) };
            break;
        case '/register':
            if (method === 'POST') response = await handleRegister(body);
            else response = { statusCode: 405, body: JSON.stringify({ success: false, error: 'Method Not Allowed' }) };
            break;
        case '/get-photos':
            if (method === 'GET') response = await handleGetPhotos(headers);
            else response = { statusCode: 405, body: JSON.stringify({ success: false, error: 'Method Not Allowed' }) };
            break;
        default:
            response = {
                statusCode: 404,
                body: JSON.stringify({ success: false, error: `接口不存在: ${path}` })
            };
    }

    return {
        ...response,
        headers: {
            ...CORS_HEADERS,
            ...(response.headers || {}),
        },
    };
};
