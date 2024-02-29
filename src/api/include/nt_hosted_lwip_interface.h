/**
 *Copyright (c) 2024, Qualcomm Innovation Center, Inc. All rights reserved.
 *SPDX-License-Identifier: ISC
 */
/*
 * nt_hosted_lwip_interface.h
 *
 *  Created on: 10-Feb-2022
 *      Author: Melon
 */

#ifndef OS_FREERTOS_LIBRARIES_3RDPARTY_LWIP_SRC_API_INCLUDE_NT_HOSTED_LWIP_INTERFACE_H_
#define OS_FREERTOS_LIBRARIES_3RDPARTY_LWIP_SRC_API_INCLUDE_NT_HOSTED_LWIP_INTERFACE_H_
#ifdef NT_HOSTED_SDK
#include "lwip/sockets.h"
#include "lwip/ip_addr.h"


#define RECV_BUF_SIZE 2048

/* Client port to connect */
#define UDP_CONN_PORT_CLIENT 5001
#define UDP_CONN_PORT_SERVER 5002

#define LWIPERF_TCP_PORT_DEFAULT  5001

/** Change this if you don't want to lwiperf to listen to any IP version */
#ifndef HOSTED_SERVER_IP_TYPE
#define HOSTED_SERVER_IP_TYPE      IPADDR_TYPE_ANY
#endif

/** Specify the idle timeout (in seconds) after that the test fails */
#ifndef HOSTED_TCP_MAX_IDLE_SEC
#define HOSTED_TCP_MAX_IDLE_SEC    250U
#endif
#ifndef HOSTED_TCP_SERVER_MAX_IDLE_SEC
#define HOSTED_TCP_SERVER_MAX_IDLE_SEC    1500
#endif

#ifndef HOSTED_TCP_SERVER_POLL_DELAY
#define HOSTED_TCP_SERVER_POLL_DELAY    10
#endif

typedef enum _hosted_conn_mode{
	hosted_if_IDLE,
	hosted_if_SERVER,
	hosted_if_CLIENT,
	hosted_if_BOTH,
	hosted_if_MAX
}hosted_conn_mode;

/** Protocol family and type of the ntconn */
typedef enum hosted_conn_type {
	hosted_conn_INVALID    = 0,
    /* NTCONN_TCP Group */
	hosted_conn_TCP        = 0x10,
    /* NTCONN_UDP Group */
	hosted_conn_UDP        = 0x20,
}protocol_type;


typedef struct _hosted_session_cntxt
{
	uint8_t link_id;
	uint8_t tid;
	protocol_type type;
	hosted_conn_mode conn_mode;
#ifdef sock
	int sock;
	struct sockaddr_in saddr;
	struct sockaddr_in saddr_dst;
#else
	void *raw_pcb;
	struct tcp_pcb *conn_pcb;
	ip_addr_t remote_ip;
	uint16_t remote_port;
	uint16_t local_port;
	uint8_t poll_count;
	uint32_t pckt_cnt;
#endif //sock
	struct _hosted_session_cntxt *pnext;
}hosted_session_cntxt;

typedef struct _hosted_data_pbuf
{
      void * data;
       uint16_t len;
}host_pbuf;

uint8_t nt_hosted_start_udp_client_start(const char * remote_addr,uint16_t remote_port);
#ifdef sock
uint8_t nt_hosted_send_pack(uint8_t *msg, uint16_t length,char * remote_ip, uint16_t remote_port);
#endif //
uint8_t nt_hosted_send_udp_pack(uint8_t *msg, uint16_t length,char * remote_ip, uint16_t remote_port);
uint8_t nt_hosted_start_udp_server_start(char * local_addr,uint16_t local_port);
uint8_t nt_hosted_start_tcp_client_start(ip_addr_t * remote_ip,uint16_t remote_port);
NT_BOOL nt_hosted_find_connection_ip(ip_addr_t *remote_ip, uint16_t remote_port,hosted_session_cntxt **pnode);
void nt_hosted_conn_list_creat(hosted_session_cntxt **phead, hosted_session_cntxt *pinsert);
uint8_t nt_hosted_conn_close(protocol_type type,ip_addr_t *remote_ip,uint16_t remote_port);
err_t nt_hosted_tcp_client_send_packet(uint8_t *msg, uint16_t length,char * remote_ip, uint16_t remote_port);
err_t nt_hosted_start_udp_server_wmm_impl(const ip_addr_t *local_addr, u16_t local_port,uint8_t tid);
err_t nt_hosted_start_udp_wmm_client(const ip_addr_t *remote_ip,u16_t remote_port, uint8_t tid);
void nt_hosted_udp_output(void *arg, struct udp_pcb *pcb, struct pbuf *pb,const ip_addr_t *addr, u16_t port);
uint8_t nt_hosted_start_tcp_server(const char * local_addr,uint16_t local_port);
void nt_hosted_push_pkt_to_host(host_pbuf  *p,void (*resp_function)(void*));
#endif//#ifdef NT_HOSTED_SDK
#endif /* OS_FREERTOS_LIBRARIES_3RDPARTY_LWIP_SRC_API_INCLUDE_NT_HOSTED_LWIP_INTERFACE_H_ */
