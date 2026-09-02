#pragma once

#ifdef __cplusplus
extern "C" {
#endif

int cmp_net_startup(void);
void cmp_net_shutdown(void);
int cmp_net_tcp_connect(const char* host, unsigned short port);
int cmp_net_tcp_send(const void* data, int len);
int cmp_net_tcp_recv_frame(void* buf, int maxlen);
int cmp_net_udp_send(const char* host, unsigned short port, const void* data, int len);
int cmp_net_udp_recv(void* buf, int maxlen);
int cmp_net_tcp_connected(void);

#ifdef __cplusplus
}
#endif
