#include "web_server.h"
#include "nvs_storage.h"
#include "esp_log.h"
#include "esp_random.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "lwip/sockets.h"
#include <string.h>
#include <stdlib.h>
#include "device_id.h"

static const char *TAG = "WEB";
static int s_server_started = 0;
static char s_password[7] = {0};
static int s_password_generated = 0;
static bool s_logged_in = false;

static const char* HOME_PAGE_TEMPLATE =
"<!DOCTYPE html>"
"<html><head><meta charset='UTF-8'>"
"<meta name='viewport' content='width=device-width, initial-scale=1.0'>"
"<title>门铃配网</title>"
"<style>"
"body{font-family:Arial;background:#0a0a0a;color:#fff;padding:20px;max-width:400px;margin:0 auto;text-align:center;}"
"h1{color:#4fc3f7;}"
".device-id{background:#1a1a2e;padding:10px;border-radius:8px;margin:15px 0;border:1px solid #333;}"
".device-id code{color:#4fc3f7;font-size:18px;font-weight:bold;letter-spacing:1px;}"
".btn{display:inline-block;padding:10px 20px;margin:10px;background:#4fc3f7;color:#000;text-decoration:none;border-radius:5px;}"
".hint{color:#888;font-size:13px;}"
"</style>"
"</head><body>"
"<h1>🔔 门铃配网</h1>"
"<div class='device-id'>"
"<p style='margin:0;color:#888;font-size:13px;'>📟 设备 ID</p>"
"<code>%s</code>"
"</div>"
"<p class='hint'>💡 在远程网页注册时需要使用此设备 ID</p>"
"<a class='btn' href='/settings'>⚙️ 修改 WiFi 设置</a>"
"</body></html>";

static const char* LOGIN_TEMPLATE =
"<!DOCTYPE html>"
"<html><head><meta charset='UTF-8'>"
"<meta name='viewport' content='width=device-width, initial-scale=1.0'>"
"<title>门铃登录</title>"
"<style>"
"body{font-family:Arial;background:#0a0a0a;color:#fff;padding:20px;max-width:400px;margin:0 auto;text-align:center;}"
"input{padding:10px;width:80%%;margin:10px;border:1px solid #4fc3f7;background:#1a1a1a;color:#fff;border-radius:5px;}"
"button{padding:10px 30px;background:#4fc3f7;color:#000;border:none;border-radius:5px;font-size:16px;}"
".error{color:#ff6b6b;}"
"</style>"
"</head><body>"
"<h1>🔐 门铃登录</h1>"
"<p>请输入日志中显示的6位密码</p>"
"<form action=\"/\" method=\"get\">"
"<input type=\"password\" name=\"pwd\" maxlength=\"6\" placeholder=\"6位数字\" required>"
"<br><button type=\"submit\">登录</button>"
"</form>"
"<div class=\"error\" id=\"errMsg\" style=\"display:%s;\">密码错误，请重试</div>"
"</body></html>";

static const char* SETTINGS_PAGE =
"<!DOCTYPE html>"
"<html><head><meta charset='UTF-8'>"
"<meta name='viewport' content='width=device-width, initial-scale=1.0'>"
"<title>配网设置</title>"
"<style>"
"body{font-family:Arial;background:#0a0a0a;color:#fff;padding:20px;max-width:400px;margin:0 auto;}"
"input{padding:10px;width:100%%;margin:5px 0;border:1px solid #4fc3f7;background:#1a1a1a;color:#fff;border-radius:5px;box-sizing:border-box;}"
"button{padding:10px 30px;background:#4fc3f7;color:#000;border:none;border-radius:5px;font-size:16px;}"
".info{color:#aaa;font-size:14px;}"
"</style>"
"</head><body>"
"<h2>📶 输入家庭 WiFi 信息</h2>"
"<form action=\"/settings\" method=\"get\">"
"<label>WiFi 名称 (SSID):</label>"
"<input type=\"text\" name=\"ssid\" maxlength=\"32\" placeholder=\"请输入WiFi名称\" value=\"%s\" required>"
"<label>WiFi 密码:</label>"
"<input type=\"text\" name=\"password\" maxlength=\"64\" placeholder=\"请输入WiFi密码\" value=\"\" required>"
"<br><button type=\"submit\">保存并重启</button>"
"</form>"
"<p class='info'>保存后设备将重启并尝试连接该 WiFi。</p>"
"<a href='/' style='color:#4fc3f7;'>← 返回主页</a>"
"</body></html>";

static const char* SUCCESS_PAGE =
"<!DOCTYPE html>"
"<html><head><meta charset='UTF-8'>"
"<meta http-equiv='refresh' content='5;url=/' />"
"<title>修改成功</title>"
"<style>body{font-family:Arial;background:#0a0a0a;color:#fff;padding:20px;text-align:center;}"
".success{color:#4fc3f7;font-size:24px;}</style>"
"</head><body>"
"<h1 class='success'>✅ 配置已保存</h1>"
"<p>设备将在 5 秒后重启，请重新连接新的 WiFi。</p>"
"<p>新 SSID: <strong>%s</strong></p>"
"<p>新密码: <strong>%s</strong></p>"
"<p>若未自动跳转，请手动重新连接。</p>"
"</body></html>";

static void generate_password(void)
{
    if (s_password_generated) return;
    uint32_t rand_num = esp_random() % 1000000;
    snprintf(s_password, sizeof(s_password), "%06u", (unsigned int)rand_num);
    s_password_generated = 1;
    s_logged_in = false;
    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, "🔑 随机密码: %s", s_password);
    ESP_LOGI(TAG, "========================================");
}

static void send_response(int sock, const char *status, const char *content_type, const char *body)
{
    char response[4096];
    int len = snprintf(response, sizeof(response),
        "HTTP/1.1 %s\r\n"
        "Content-Type: %s; charset=utf-8\r\n"
        "Connection: close\r\n"
        "\r\n"
        "%s",
        status, content_type, body);
    send(sock, response, len, 0);
}

static void send_redirect(int sock, const char *location)
{
    char response[256];
    snprintf(response, sizeof(response),
        "HTTP/1.1 302 Found\r\n"
        "Location: %s\r\n"
        "Connection: close\r\n"
        "\r\n",
        location);
    send(sock, response, strlen(response), 0);
}

static int parse_param(const char *uri, const char *key, char *value, int max_len)
{
    char *p = strstr(uri, key);
    if (!p) return 0;
    p += strlen(key);
    if (*p != '=') return 0;
    p++;
    int i = 0;
    while (*p && *p != '&' && i < max_len - 1) {
        value[i++] = *p++;
    }
    value[i] = '\0';
    return 1;
}

static void restart_timer_callback(void *arg)
{
    ESP_LOGI(TAG, "重启设备以应用新配置...");
    esp_restart();
}

static void handle_settings(int sock, const char *uri)
{
    saved_wifi_config_t sta_cfg;
    esp_err_t ret = nvs_load_wifi_config(&sta_cfg);
    char current_ssid[64] = "";
    if (ret == ESP_OK && sta_cfg.is_configured) {
        strncpy(current_ssid, sta_cfg.ssid, sizeof(current_ssid) - 1);
    }

    char new_ssid[33] = {0};
    char new_password[65] = {0};
    int got_ssid = parse_param(uri, "ssid", new_ssid, sizeof(new_ssid));
    int got_pwd = parse_param(uri, "password", new_password, sizeof(new_password));

    if (got_ssid && got_pwd && strlen(new_ssid) > 0 && strlen(new_password) >= 8) {
        saved_wifi_config_t new_cfg;
        strncpy(new_cfg.ssid, new_ssid, sizeof(new_cfg.ssid) - 1);
        strncpy(new_cfg.password, new_password, sizeof(new_cfg.password) - 1);
        new_cfg.is_configured = 1;
        esp_err_t save_ret = nvs_save_wifi_config(&new_cfg);
        if (save_ret == ESP_OK) {
            char body[2048];
            snprintf(body, sizeof(body), SUCCESS_PAGE, new_ssid, new_password);
            send_response(sock, "200 OK", "text/html", body);
            esp_timer_handle_t restart_timer;
            esp_timer_create_args_t timer_args = {
                .callback = restart_timer_callback,
                .name = "restart_timer"
            };
            ESP_ERROR_CHECK(esp_timer_create(&timer_args, &restart_timer));
            ESP_ERROR_CHECK(esp_timer_start_once(restart_timer, 2000000));
        } else {
            char err_page[512];
            snprintf(err_page, sizeof(err_page),
                "<html><body><h1>❌ 保存失败</h1><p>错误码: %d</p><a href='/settings'>重试</a></body></html>", save_ret);
            send_response(sock, "500 Internal Server Error", "text/html", err_page);
        }
        return;
    }

    char body[4096];
    snprintf(body, sizeof(body), SETTINGS_PAGE, current_ssid);
    send_response(sock, "200 OK", "text/html", body);
}

static void http_server_thread(void *arg)
{
    int server_sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (server_sock < 0) {
        ESP_LOGE(TAG, "创建socket失败");
        vTaskDelete(NULL);
        return;
    }

    int opt = 1;
    setsockopt(server_sock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in addr = {
        .sin_family = AF_INET,
        .sin_port = htons(80),
        .sin_addr = { htonl(INADDR_ANY) }
    };
    if (bind(server_sock, (struct sockaddr*)&addr, sizeof(addr)) != 0) {
        ESP_LOGE(TAG, "绑定端口失败");
        close(server_sock);
        vTaskDelete(NULL);
        return;
    }

    if (listen(server_sock, 5) != 0) {
        ESP_LOGE(TAG, "监听失败");
        close(server_sock);
        vTaskDelete(NULL);
        return;
    }

    ESP_LOGI(TAG, "Web服务器已启动 (端口80)");

    while (1) {
        struct sockaddr_in client_addr;
        socklen_t addr_len = sizeof(client_addr);
        int client_sock = accept(server_sock, (struct sockaddr*)&client_addr, &addr_len);
        if (client_sock < 0) continue;

        char buf[1024];
        int len = recv(client_sock, buf, sizeof(buf)-1, 0);
        if (len <= 0) {
            close(client_sock);
            continue;
        }
        buf[len] = '\0';

        char method[16], uri[256], version[16];
        if (sscanf(buf, "%s %s %s", method, uri, version) != 3) {
            close(client_sock);
            continue;
        }

        if (strcmp(method, "GET") == 0 && strncmp(uri, "/settings", 9) == 0) {
            handle_settings(client_sock, uri);
            close(client_sock);
            continue;
        }

        if (strcmp(method, "GET") == 0 && (strncmp(uri, "/", 1) == 0 || strncmp(uri, "/?", 2) == 0)) {
            int pwd_correct = 0;
            int has_pwd_error = 0;
            char *pwd_pos = strstr(uri, "?pwd=");
            if (pwd_pos) {
                char *pwd_val = pwd_pos + 5;
                char pwd_buf[7] = {0};
                int i;
                for (i = 0; i < 6 && pwd_val[i] >= '0' && pwd_val[i] <= '9'; i++) {
                    pwd_buf[i] = pwd_val[i];
                }
                if (i == 6 && strcmp(pwd_buf, s_password) == 0) {
                    pwd_correct = 1;
                } else {
                    has_pwd_error = 1;
                }
            }

            if (!pwd_correct) {
                char login_page[2048];
                snprintf(login_page, sizeof(login_page), LOGIN_TEMPLATE, has_pwd_error ? "block" : "none");
                send_response(client_sock, "200 OK", "text/html", login_page);
                close(client_sock);
                continue;
            }

            s_logged_in = true;
            saved_wifi_config_t sta_cfg;
            esp_err_t ret = nvs_load_wifi_config(&sta_cfg);
            int is_first_time = (ret != ESP_OK || !sta_cfg.is_configured);
            if (is_first_time) {
                send_redirect(client_sock, "/settings");
            } else {
                const char *dev_id = device_id_get();
                char home_page[4096];
                snprintf(home_page, sizeof(home_page), HOME_PAGE_TEMPLATE, dev_id ? dev_id : "未知");
                send_response(client_sock, "200 OK", "text/html", home_page);
            }
            close(client_sock);
            continue;
        }

        send_response(client_sock, "404 Not Found", "text/plain", "404");
        close(client_sock);
    }

    close(server_sock);
    vTaskDelete(NULL);
}

esp_err_t web_server_init(void)
{
    if (s_server_started) {
        return ESP_OK;
    }

    generate_password();

    xTaskCreate(http_server_thread, "http_server", 16384, NULL, 5, NULL);
    s_server_started = 1;

    ESP_LOGI(TAG, "✅ Web服务器已启动，请访问 http://192.168.4.1 并输入密码");
    return ESP_OK;
}

const char* web_server_get_password(void)
{
    return s_password;
}

bool web_server_is_logged_in(void)
{
    return s_logged_in;
}

void web_server_add_photo(const char *filename, size_t size) {}
int web_server_get_photo_count(void) { return 0; }
void web_server_clear_photos(void) {}