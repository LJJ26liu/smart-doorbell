'use strict';

const TableStore = require('tablestore');
const OSS = require('ali-oss');

const client = new TableStore.Client({
    accessKeyId: process.env.OTS_ACCESS_KEY_ID,
    accessKeySecret: process.env.OTS_ACCESS_KEY_SECRET,
    endpoint: 'https://doorbell-data.cn-shenzhen.ots.aliyuncs.com',
    instancename: 'doorbell-data'
});

const ossClient = new OSS({
    accessKeyId: process.env.OTS_ACCESS_KEY_ID,
    accessKeySecret: process.env.OTS_ACCESS_KEY_SECRET,
    region: 'oss-cn-guangzhou',
    bucket: 'smart-doorbell-photos',
});

const tableName = 'users';

// ===== 验证用户 =====
async function verifyUser(email) {
    if (!email) return null;
    const getParams = {
        tableName,
        primaryKey: [{ 'email': email }],
        maxVersions: 1
    };
    try {
        const result = await client.getRow(getParams);
        if (!result.row || !result.row.attributes) return null;
        const deviceId = String(result.row.attributes.find(attr => attr.columnName === 'deviceId')?.columnValue || '').trim();
        return { email, deviceId };
    } catch (err) {
        console.error('验证用户失败:', err);
        return null;
    }
}

// ===== 生成签名 URL =====
function generateSignedUrl(deviceId, filename, expires = 300) {
    const objectKey = `${deviceId}/${filename}`;
    return ossClient.signatureUrl(objectKey, {
        expires: expires,
        method: 'GET'
    });
}

// ===== 注册 =====
async function handleRegister(body, corsHeaders) {
    const { email, password, deviceId } = body;
    if (!email || !password || !deviceId) {
        return {
            statusCode: 400,
            headers: corsHeaders,
            body: JSON.stringify({ success: false, error: '缺少必要字段' }),
        };
    }
    if (password.length < 6) {
        return {
            statusCode: 400,
            headers: corsHeaders,
            body: JSON.stringify({ success: false, error: '密码至少6位' }),
        };
    }

    const getParams = {
        tableName,
        primaryKey: [{ 'email': email }],
        maxVersions: 1
    };
    const getResult = await client.getRow(getParams);
    if (getResult.row && getResult.row.primaryKey) {
        return {
            statusCode: 400,
            headers: corsHeaders,
            body: JSON.stringify({ success: false, error: '该邮箱已注册' }),
        };
    }

    const putParams = {
        tableName,
        condition: new TableStore.Condition(TableStore.RowExistenceExpectation.EXPECT_NOT_EXIST),
        primaryKey: [{ 'email': email }],
        attributeColumns: [
            { 'password': password },
            { 'deviceId': deviceId },
            { 'createdAt': new Date().toISOString() }
        ]
    };
    await client.putRow(putParams);

    return {
        statusCode: 200,
        headers: corsHeaders,
        body: JSON.stringify({ success: true, message: '注册成功' }),
    };
}

// ===== 登录 =====
async function handleLogin(body, corsHeaders) {
    const { email, password } = body;
    if (!email || !password) {
        return {
            statusCode: 400,
            headers: corsHeaders,
            body: JSON.stringify({ success: false, error: '请输入邮箱和密码' }),
        };
    }

    const getParams = {
        tableName,
        primaryKey: [{ 'email': email }],
        maxVersions: 1
    };
    const getResult = await client.getRow(getParams);
    if (!getResult.row || !getResult.row.attributes) {
        return {
            statusCode: 401,
            headers: corsHeaders,
            body: JSON.stringify({ success: false, error: '邮箱或密码错误' }),
        };
    }

    const storedPassword = String(getResult.row.attributes.find(attr => attr.columnName === 'password')?.columnValue || '').trim();
    const storedDeviceId = String(getResult.row.attributes.find(attr => attr.columnName === 'deviceId')?.columnValue || '').trim();

    if (storedPassword !== String(password).trim()) {
        return {
            statusCode: 401,
            headers: corsHeaders,
            body: JSON.stringify({ success: false, error: '邮箱或密码错误' }),
        };
    }

    return {
        statusCode: 200,
        headers: corsHeaders,
        body: JSON.stringify({
            success: true,
            user: { email, deviceId: storedDeviceId }
        }),
    };
}

// ===== 获取照片列表（后端自动根据用户 deviceId 读取 OSS） =====
async function handleGetPhotos(queryParams, corsHeaders, evt) {
    const userEmail = evt.headers && (evt.headers['x-user-email'] || evt.headers['X-User-Email']);

    if (!userEmail) {
        return {
            statusCode: 401,
            headers: corsHeaders,
            body: JSON.stringify({ success: false, error: '未登录' }),
        };
    }

    const user = await verifyUser(userEmail);
    if (!user) {
        return {
            statusCode: 401,
            headers: corsHeaders,
            body: JSON.stringify({ success: false, error: '用户不存在' }),
        };
    }

    const deviceId = user.deviceId;
    if (!deviceId) {
        return {
            statusCode: 400,
            headers: corsHeaders,
            body: JSON.stringify({ success: false, error: '用户未绑定设备ID' }),
        };
    }

    try {
        const objectKey = `${deviceId}/${deviceId}.json`;
        let photos = [];
        try {
            const result = await ossClient.get(objectKey);
            const jsonContent = result.content.toString('utf-8');
            photos = JSON.parse(jsonContent);
        } catch (err) {
            if (err.code === 'NoSuchKey') {
                return {
                    statusCode: 200,
                    headers: corsHeaders,
                    body: JSON.stringify([]),
                };
            }
            throw err;
        }

        if (!Array.isArray(photos)) {
            return {
                statusCode: 500,
                headers: corsHeaders,
                body: JSON.stringify({ success: false, error: '数据格式错误' }),
            };
        }

        const result = photos.map(item => {
            const filename = item.filename || '';
            if (!filename) return null;
            const signedUrl = generateSignedUrl(deviceId, filename, 300);
            return {
                filename: filename,
                type: item.type || 'pass',
                timestamp: item.timestamp || Date.now(),
                url: signedUrl,
                deviceId: deviceId
            };
        }).filter(p => p !== null);

        return {
            statusCode: 200,
            headers: corsHeaders,
            body: JSON.stringify(result),
        };

    } catch (err) {
        console.error('读取照片列表失败:', err);
        return {
            statusCode: 500,
            headers: corsHeaders,
            body: JSON.stringify({ success: false, error: '读取照片列表失败: ' + err.message }),
        };
    }
}

// ===== 入口 =====
exports.handler = async (event, context, callback) => {
    let evt = event;
    if (Buffer.isBuffer(event) || (event && event.type === 'Buffer')) {
        try {
            const buf = Buffer.from(event.data || event);
            evt = JSON.parse(buf.toString('utf-8'));
        } catch (e) {
            console.error('解析 Buffer 失败:', e);
            return {
                statusCode: 400,
                headers: { 'Content-Type': 'application/json', 'Access-Control-Allow-Origin': '*' },
                body: JSON.stringify({ success: false, error: '请求格式错误' }),
            };
        }
    }

    const method = (evt.requestContext && evt.requestContext.http && evt.requestContext.http.method) ||
                   evt.httpMethod || evt.method || '';
    const path = (evt.requestContext && evt.requestContext.http && evt.requestContext.http.path) ||
                 evt.path || evt.rawPath || '/';
    const queryParams = evt.queryStringParameters || evt.query || {};

    const corsHeaders = {
        'Content-Type': 'application/json',
        'Access-Control-Allow-Origin': '*',
        'Access-Control-Allow-Headers': 'Content-Type, Authorization, X-User-Email',
        'Access-Control-Allow-Methods': 'GET, POST, OPTIONS',
    };

    if (method.toUpperCase() === 'OPTIONS') {
        return {
            statusCode: 200,
            headers: corsHeaders,
            body: '',
        };
    }

    let body = {};
    if (evt.body) {
        try {
            body = typeof evt.body === 'string' ? JSON.parse(evt.body) : evt.body;
        } catch (e) {
            return {
                statusCode: 400,
                headers: corsHeaders,
                body: JSON.stringify({ success: false, error: '请求体格式错误' }),
            };
        }
    }

    try {
        if (method.toUpperCase() === 'POST' && path === '/register') {
            return await handleRegister(body, corsHeaders);
        } else if (method.toUpperCase() === 'POST' && path === '/login') {
            return await handleLogin(body, corsHeaders);
        } else if (method.toUpperCase() === 'GET' && path === '/get-photos') {
            return await handleGetPhotos(queryParams, corsHeaders, evt);
        } else {
            return {
                statusCode: 404,
                headers: corsHeaders,
                body: JSON.stringify({ success: false, error: '接口不存在' }),
            };
        }
    } catch (err) {
        console.error('处理请求出错:', err);
        return {
            statusCode: 500,
            headers: corsHeaders,
            body: JSON.stringify({ success: false, error: '服务器内部错误' }),
        };
    }
};