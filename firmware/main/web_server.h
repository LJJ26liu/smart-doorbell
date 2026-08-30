#ifndef WEB_SERVER_H
#define WEB_SERVER_H

#include "esp_err.h"
#include <stdbool.h>

// 照片信息结构体（用于网页展示列表）
typedef struct {
    char filename[64];
    size_t size;
    int is_new;
} photo_info_t;

esp_err_t web_server_init(void);
const char* web_server_get_password(void);

// 查询用户是否已登录（输入正确密码）
bool web_server_is_logged_in(void);

void web_server_add_photo(const char *filename, size_t size);
int web_server_get_photo_count(void);
void web_server_clear_photos(void);

#endif
