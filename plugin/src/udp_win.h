#pragma once

#ifdef __cplusplus
extern "C" {
#endif

int cmp_udp_startup(void);
void cmp_udp_shutdown(void);
int cmp_udp_send(const char* host, unsigned short port, const void* data, int len);
int cmp_udp_recv(void* buf, int maxlen);

#ifdef __cplusplus
}
#endif
