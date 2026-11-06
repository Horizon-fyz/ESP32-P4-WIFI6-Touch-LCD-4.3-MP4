#pragma once

#ifdef __cplusplus
extern "C" {
#endif

/* 启动内置 HTTP 服务（WiFi 获取 IP 后调用），可重复调用。 */
void web_server_start(void);

#ifdef __cplusplus
}
#endif
