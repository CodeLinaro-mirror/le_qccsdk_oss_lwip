/*
 * Copyright (c) 2024 Qualcomm Innovation Center, Inc. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */
/*
 *  Created on: 10-Feb-2022
 *      Author: Melon
 */
#ifdef NT_HOSTED_SDK
#include "network_al.h"
#include "inet.h"
#include "include/nt_hosted_lwip_interface.h"
#include "lwip/udp.h"
#include "lwip/tcp.h"
#include "lwip/debug.h"
#include "lwip/inet.h"
#include "lwip/tcpip.h"
#include "nt_logger_api.h"
#include  "wifi_app.h"

extern qurt_pipe_t x_spiQueueHandle;
uint8_t is_traffic_runnig = FALSE;
nt_osal_semaphore_handle_t hosted_lwip_SemaphoreHandle = NULL;
wapp_msg_struct_t hosted_msg;

void nt_hosted_result_fn()
{
	nt_osal_semaphore_give(hosted_lwip_SemaphoreHandle);
}

static void dbg_print_lwiperf_udp( char *title, uint32_t data)
{
	char pbuf[200];
	snprintf(pbuf,sizeof(pbuf), "%s %u\r\n", title, (unsigned int)data);
	nt_dbg_print(pbuf);
}

hosted_session_cntxt * hosted_session_list=NULL;
 uint8_t nt_hosted_start_udp_client_start(const char * remote_addr,uint16_t remote_port)
{
#ifdef sock
	void *nt_sock = NULL;
	uint8_t err = NT_OK;
	socklen_t soclen;
	ip_addr_t local_ip;
	ip_addr_t remote_ip;

	(void )remote_port;
	int sock = -1;
	struct sockaddr_in saddr_dst = { 0 };
	struct sockaddr_in saddr = { 0 };

	ipaddr_aton(remote_addr,&remote_ip );
	if(IP_IS_V4_VAL(remote_ip)){

			local_ip = *nt_dpm_get_ip(netif_get_by_index(DEFAULT_NETIF_IDX),IPADDR_TYPE_V4,0);

		}else{
			local_ip = *nt_dpm_get_ip(netif_get_by_index(DEFAULT_NETIF_IDX),IPADDR_TYPE_V6,0);
		}

	sock = socket(PF_INET, SOCK_DGRAM, IPPROTO_UDP);
	if (sock < 0) {
#ifdef NT_DEBUG
		NT_LOG_DPM_INFO("Failed to create socket. Error ", errno,0,0);
#endif
		return sock;
	}

	saddr_dst.sin_family = PF_INET;
	saddr_dst.sin_port = htons(remote_port);
	saddr_dst.sin_addr.s_addr = htonl(remote_ip.u_addr.ip4.addr);

	// Bind the socket to any address
	saddr.sin_family = PF_INET;
	saddr.sin_port = htons(UDP_CONN_PORT_CLIENT);
	saddr.sin_addr.s_addr = htonl(local_ip.u_addr.ip4.addr);
	soclen = sizeof(struct sockaddr_in);
	nt_sock = &saddr;

	err = bind(sock, (struct sockaddr*) nt_sock, soclen);
	if (err < 0) {
		dbg_print_lwiperf_udp("Failed to bind socket. Error \r\n", errno);
	}
	return err;
#else

	ip_addr_t local_ip;
	ip_addr_t remote_ip;
	struct udp_pcb * udpecho_raw_pcb_client ;

	ipaddr_aton(remote_addr,&remote_ip );
	if(IP_IS_V4_VAL(remote_ip)){

		local_ip = *nt_dpm_get_ip(netif_get_by_index(DEFAULT_NETIF_IDX),IPADDR_TYPE_V4,0);

	}else{
		local_ip = *nt_dpm_get_ip(netif_get_by_index(DEFAULT_NETIF_IDX),IPADDR_TYPE_V6,0);
	}

	udpecho_raw_pcb_client = udp_new_ip_type(IPADDR_TYPE_ANY);

		if (udpecho_raw_pcb_client == NULL) {

			return NT_ENOMEM;
		}

		udpecho_raw_pcb_client->remote_ip = remote_ip;
		udpecho_raw_pcb_client->local_ip = local_ip;
		udpecho_raw_pcb_client->remote_port = remote_port;

		err_t err;
		err = udp_bind(udpecho_raw_pcb_client, &udpecho_raw_pcb_client->local_ip, UDP_CONN_PORT_CLIENT);
#ifdef NT_DEBUG
		NT_LOG_DPM_INFO("udp_bind", err,0,0);
#endif
		hosted_session_cntxt * sess_cntxt = (hosted_session_cntxt *)nt_osal_calloc(1,sizeof(hosted_session_cntxt));

		udp_recv(udpecho_raw_pcb_client,nt_hosted_udp_output, sess_cntxt);

		if (err == ERR_OK) {
			err = udp_connect(udpecho_raw_pcb_client, &remote_ip, remote_port);
#ifdef NT_DEBUG
			NT_LOG_DPM_INFO("udp_connected",err,0,0);
#endif
			if (err != ERR_OK) {
				configASSERT(0);
				return err;
			}
			nt_at_ok_error(NT_AT_UDP_SUCESS,0,NULL);
			sess_cntxt->raw_pcb = (void *)udpecho_raw_pcb_client;
			ip_addr_set(&sess_cntxt->remote_ip,  &remote_ip);
			sess_cntxt->remote_port = remote_port;
			nt_hosted_conn_list_creat(&hosted_session_list,sess_cntxt);
		}
		return err;



#endif//sock


}



#ifdef sock
uint8_t nt_hosted_send_pack(uint8_t *msg, uint16_t length,char * remote_ip, uint16_t remote_port)
{
	int ret = NT_FAIL;
	hosted_session_cntxt  cur_session;
	if(nt_hosted_find_connection_ip(remote_ip, remote_port,&cur_session))
	{
		ret = sendto(cur_session.sock,msg, length,0, &cur_session.saddr_dst, 1);
		if (ret ==length)
		{
			if(cur_session.type== IPPROTO_UDP)
			{
				nt_at_ok_error(NT_AT_UDP_SUCESS,0,NULL);
			}
			else if(cur_session.type == IPPROTO_TCP)
			{
				nt_at_ok_error(NT_AT_TCP_SEND_SUCESS,0,NULL);

			}
			ret = NT_OK;
		}

	}
	return ret;

}
#else

struct pbuf *pb = NULL;
uint8_t nt_hosted_send_udp_pack(uint8_t *msg, uint16_t length,char * remote_addr, uint16_t remote_port)
{

hosted_session_cntxt * cur_session;
ip_addr_t remote_ip;
ipaddr_aton(remote_addr, &remote_ip);
struct pbuf *q ,*p_temp;
   u8_t *data = NULL;
   u16_t cnt = 0;
   u16_t i = 0;
   err_t err;
   uint32_t *payload;

NT_BOOL status =nt_hosted_find_connection_ip(&remote_ip, remote_port,&cur_session);

if(status)
{
//	pb = pbuf_alloc(PBUF_RAW, length, PBUF_RAM);
//	if (NULL == pb) {
//		configASSERT(0);
//		return ret;
//	}
//	pb->payload = msg;
//	pb->len = pb->tot_len = length;
//	while (1) {
//
//		ret = udp_send((struct udp_pcb *)cur_session->raw_pcb, pb);
//
//
//		if ((ret == ERR_MEM) || (ret == ERR_INPROGRESS)) {
//			qurt_thread_sleep(2);
//		} else {
//			break;
//		}
//
//	}
//
//
//	if (ret == ERR_OK) {
//		nt_at_ok_error(NT_AT_UDP_SUCESS,0,NULL);
//	} else {
//		nt_at_ok_error(NT_AT_UDP_ERR,0,NULL);
//	}
//
//	pbuf_free(pb);




	if(pb == NULL)
	{
		pb = pbuf_alloc(PBUF_TRANSPORT, length, PBUF_RAM);
		pb->type_internal |= PBUF_TYPE_FLAG_DATA_VOLATILE;
	}


    if (pb != NULL) {
        q = pb;

        while (q != NULL) {
            data = (u8_t *)q->payload;

            for (i = 0; i < q->len; i++) {

                data[i] = ((u8_t *) msg)[cnt++];
            }
            q = q->next;
        }
    } else {
        return NT_ENOMEM;
    }

    pb->len = pb->tot_len = length;
    payload = (uint32_t *) (pb->payload);

    *payload = htonl((struct udp_pcb *)cur_session->pckt_cnt);
    (struct udp_pcb *)cur_session->pckt_cnt++;
    while(1) {


    	if ((uint32_t)payload != (uint32_t)pb->payload) {
    			if ((uint32_t)payload > (uint32_t)pb->payload) {
    				pbuf_remove_header(pb, (uint32_t)payload - (uint32_t)pb->payload);
    			} else {
    				dbg_print_lwiperf_udp("payload error", (uint32_t)pb->payload);
    			}
    		}

    		if ((uint32_t)payload != (uint32_t)pb->payload) {
    			dbg_print_lwiperf_udp("payload mismatch", (uint32_t)payload);
    		}

    		if (length != (uint32_t)pb->tot_len) {
    			dbg_print_lwiperf_udp("payload len mismatch", (uint32_t)pb->tot_len);
    		}

    		LOCK_TCPIP_CORE();
    		err = udp_send((struct udp_pcb *)cur_session->raw_pcb,pb);
    		UNLOCK_TCPIP_CORE();

    		if ((err == ERR_MEM) || (err == ERR_INPROGRESS)) {
    			qurt_thread_sleep(0);
    		} else {

    			break;
    		}
    }

    if (pb->ref != 0) {

//        pbuf_free(p);
      //  ntconn_data_sent(pudp_sent, NTCONN_SEND);
    	if(err == ERR_OK)
    	    	{
    	    		nt_at_ok_error(NT_AT_UDP_SUCESS,0,NULL);
    	    	}
        if (err == ERR_IF)
        	return NT_EIF;
        return err;
    } else {

    	if(err == ERR_OK)
    	{
    		nt_at_ok_error(NT_AT_UDP_SUCESS,0,NULL);
    	}

    	//pbuf_free(p);
    	if(err == ERR_RTE)
    		err = NT_ERTE;
    	return err;
    }

}

else

{
#ifdef NT_DEBUG
	NT_LOG_DPM_INFO("something wrong",0,0,0);
#endif
}

return err;


}
#endif //sock



void nt_hosted_udp_output(void *arg, struct udp_pcb *pcb, struct pbuf *pb,const ip_addr_t *addr, u16_t port)
{

	(void)pcb;
	(void)addr;
	(void)port;
	if(*(int *)pb->payload == htonl(-1))
	{
			udp_disconnect(pcb);
			udp_remove(pcb);
			nt_at_ok_error(NT_AT_SERVER_CLOSED,0,NULL);
	}
	else
	{
		nt_at_ok_error(NT_AT_UDP_MSG_RCVD,0,NULL);
		//UART_Send((char *)pb->payload,pb->tot_len);

		char c ='>';
		nt_at_spi_send_of_len(&c,1);
		nt_at_spi_data_of_len_with_premtion(pb->payload,pb->tot_len);
		c ='<';
		nt_at_spi_send_of_len(&c,1);
	}


	pbuf_free(pb);

}

uint8_t nt_hosted_start_udp_server_start(char * local_addr,uint16_t local_port)
{

#ifdef sock
	ip_addr_t local_ip;
	local_ip = *nt_dpm_get_ip(netif_get_by_index(DEFAULT_NETIF_IDX),IPADDR_TYPE_V4,0);
	uint8_t buffer[RECV_BUF_SIZE];
    int sockfd;
    struct sockaddr_in servaddr, cliaddr;
	int addrlen;
	int len;
	uint8_t ret = NT_FAIL;

    // Creating socket file descriptor
    if ((sockfd = socket(AF_INET, SOCK_DGRAM, 0)) < 0 ) {
#ifdef NT_DEBUG
    	NT_LOG_DPM_INFO("socket creation failed", sockfd,0,0);
#endif
    	return NT_FAIL;
    }

    memset(&servaddr, 0, sizeof(servaddr));
    memset(&cliaddr, 0, sizeof(cliaddr));

    // Filling server information
    servaddr.sin_family    = AF_INET; // IPv4
    servaddr.sin_addr.s_addr = htonl(local_ip.u_addr.ip4.addr);
    servaddr.sin_port = htons(UDP_CONN_PORT_SERVER);

    // Bind the socket with the server address
    if (bind(sockfd, (const struct sockaddr *)&servaddr,
            sizeof(servaddr)) < 0) {
        perror("bind failed");
        exit(EXIT_FAILURE);
    }

	addrlen = sizeof(cliaddr);  //len is value/resuslt

	len = recvfrom(sockfd, (char *)buffer, RECV_BUF_SIZE,
				MSG_WAITALL, ( struct sockaddr *) &cliaddr,
				&addrlen);

	if (len > 0) {
//#ifdef NT_HOSTED_SDK
//		if(NTCONN_TYPE_UDP(conn)){
//		nt_at_ok_error(NT_AT_UDP_MSG_RCVD,1,buff);
//		}
//		else if (NTCONN_TYPE_TCP(conn)){
//
//			nt_at_ok_error(NT_AT_TCP_MSG_RCVD,1,buff);
//		}
		char udp_spi_buff[55];
		#ifdef NT_HOSTED_SDK
			snprintf(udp_spi_buff, sizeof(udp_spi_buff), "Data sent to %s:%d \r\n",
						ipaddr_ntoa(&cliaddr.sin_addr.s_addr),cliaddr.sin_port);
		nt_at_ok_error(NT_AT_UDP_MSG_RCVD,1,udp_spi_buff);
		nt_at_spi_send_of_len(buffer,len);
#endif//NT_HOSTED_SDK
		ret  = NT_OK;

	}
	return ret;

#else
	ip_addr_t local_ip;
	ipaddr_aton(local_addr,&local_ip);//*nt_dpm_get_ip(netif_get_by_index(DEFAULT_NETIF_IDX),IPADDR_TYPE_V4,0);
	struct udp_pcb *udpecho_raw_pcb_server;
	udpecho_raw_pcb_server = udp_new_ip_type(IPADDR_TYPE_ANY);
	udpecho_raw_pcb_server->local_ip = local_ip;
	udpecho_raw_pcb_server->remote_port = local_port;
	if (udpecho_raw_pcb_server != NULL) {
		err_t err;
		err = udp_bind(udpecho_raw_pcb_server, &local_ip, local_port);
		if (err == ERR_OK) {
			//dbg_print_lwiperf_udp("udp_connected",err);
			hosted_session_cntxt * sess_cntxt = (hosted_session_cntxt *)nt_osal_calloc(1,sizeof(hosted_session_cntxt));
			sess_cntxt->raw_pcb = (void *)udpecho_raw_pcb_server;
			sess_cntxt->conn_mode =  hosted_if_SERVER;
			sess_cntxt->type = hosted_conn_UDP;
			sess_cntxt->local_port = local_port;
			nt_hosted_conn_list_creat(&hosted_session_list,sess_cntxt);

			udp_recv(udpecho_raw_pcb_server, nt_hosted_udp_output, &sess_cntxt);
			if (err != ERR_OK) {
				nt_osal_free_memory(sess_cntxt);
				configASSERT(0);
			}
		}
	}

#endif //sock



}


static err_t
nt_hosted_tcp_client_sent(void *arg, struct tcp_pcb *tpcb, u16_t len)
{
	hosted_session_cntxt *conn = (hosted_session_cntxt *)arg;
  /* @todo: check 'len' (e.g. to time ACK of all data)? for now, we just send more... */
  LWIP_ASSERT("invalid conn", (struct tcp_pcb *)conn->raw_pcb == tpcb);
  LWIP_UNUSED_ARG(tpcb);
  LWIP_UNUSED_ARG(len);

  conn->poll_count = 0;
  nt_at_ok_error(NT_AT_TCP_SEND_SUCESS,1,"TCP SEND PACK SUCESS");
  return ERR_OK;
}

/** Close an iperf tcp session */
static void
nt_hosted_tcp_close(hosted_session_cntxt *conn)
{
	err_t err;
	if (conn->conn_pcb != NULL) {
		tcp_arg(conn->conn_pcb, NULL);
		tcp_poll(conn->conn_pcb, NULL, 0);
		tcp_sent(conn->conn_pcb, NULL);
		tcp_recv(conn->conn_pcb, NULL);
		tcp_err(conn->conn_pcb, NULL);
		err = tcp_close(conn->conn_pcb);
		if (err != ERR_OK) {
			/* don't want to wait for free memory here... */
			tcp_abort(conn->conn_pcb);
		}
	}
	 if (conn->raw_pcb) {
			/* no conn pcb, this is the listener pcb */
			err = tcp_close(conn->raw_pcb);
			LWIP_ASSERT("error", err == ERR_OK);
		  }
	is_traffic_runnig=FALSE;
	nt_hosted_list_delete(&hosted_session_list, conn);
	nt_osal_free_memory(conn);
}



/** TCP connected callback (active connection), send data now */
err_t
nt_hosted_tcp_client_connected(void *arg, struct tcp_pcb *tpcb, err_t err)
{
 hosted_session_cntxt *conn = (hosted_session_cntxt *)arg;
  LWIP_ASSERT("invalid conn", (struct tcp_pcb *)conn->raw_pcb == tpcb);
  LWIP_UNUSED_ARG(tpcb);
  if (err != ERR_OK) {
    nt_hosted_tcp_close(conn);
    return ERR_OK;
  }

  conn->poll_count = 0;
  nt_at_ok_error(NT_AT_TCP_CNX_SUCESS,0,NULL);


  return ERR_OK;
}


/** Error callback, iperf tcp session aborted */
static void
nt_hosted_tcp_err(void *arg, err_t err)
{
  hosted_session_cntxt *conn = (hosted_session_cntxt *)arg;
  LWIP_UNUSED_ARG(err);
  nt_hosted_tcp_close(conn);
}


/** TCP poll callback, try to send more data */
static err_t
nt_hosted_tcp_poll(void *arg, struct tcp_pcb *tpcb)
{
	hosted_session_cntxt *conn = (hosted_session_cntxt *)arg;
	uint32_t poll_limit;
	if(conn->conn_mode == hosted_if_SERVER)
	{
  LWIP_ASSERT("pcb mismatch", conn->conn_pcb == tpcb);
  poll_limit = HOSTED_TCP_SERVER_MAX_IDLE_SEC; //conn->timeout
	}
	else if(conn->conn_mode == hosted_if_CLIENT)
	{
		LWIP_ASSERT("pcb mismatch", (struct tcp_pcb *)conn->raw_pcb == tpcb);
		poll_limit = HOSTED_TCP_MAX_IDLE_SEC;
	}
  LWIP_UNUSED_ARG(tpcb);
  if (++conn->poll_count >= poll_limit) {
	  nt_hosted_tcp_close(conn);
    return ERR_OK; /* lwiperf_tcp_close frees conn */
  }
  vTaskDelay(HOSTED_TCP_SERVER_POLL_DELAY);
  return ERR_OK;
}



uint8_t nt_hosted_start_tcp_client_start(ip_addr_t * remote_ip,uint16_t remote_port)
{

#ifdef sock
	struct sockaddr_in sin;
	/* TCP Data buffer */
	static char data_buffer[150];
	/* ID Client */
	int CControl;
	uint8_t ret;

	/* Fill structure */
	memset(&sin,0,sizeof(sin));
	sin.sin_family = AF_INET;
	sin.sin_port = htons(remote_port);
	sin.sin_addr.s_addr = inet_addr(remoteip);//inet_ntoa(remoteip);

	/* Get socket */
	CControl = socket(PF_INET, SOCK_STREAM, IPPROTO_TCP);
	if (CControl == -1) {
		/* Impossible get a socket */
#ifdef NT_DEBUG
		NT_LOG_DPM_INFO("Impossible get a socket\r\n",0,0,0);
#endif
		nt_osal_thread_delete( NULL );
		return CControl;
	}

		/* Connect to remote TCP server */
		ret = connect(CControl, (struct sockaddr *)&sin, sizeof(sin));	//block till connection established
		//
		if (ret == -1) {
			/* Close socket */
			closesocket(CControl);
			/* Impossible connect to remote TCP server */
			("Impossible connect to remote TCP server\r\n",0,0,0);
			return ret;
		}
#ifdef NT_DEBUG
		NT_LOG_DPM_INFO("client connected with server", (uint32_t)ret,0,0);
#endif
		//ret = send(CControl,data_buffer, sizeof(data_buffer), 0);


#else



			  err_t err;
			  hosted_session_cntxt *client_conn;
			  struct tcp_pcb *newpcb;
			  BaseType_t ret;
			  ip_addr_t local_ip;

			  ip_addr_t remote_addr;

			 // ipaddr_aton(remote_addr,&client_conn->remote_ip);
			  LWIP_ASSERT("remote_ip != NULL",remote_ip/* &client_conn->remote_ip */!= NULL);

			  client_conn = (hosted_session_cntxt *)nt_osal_calloc(1,sizeof(hosted_session_cntxt));
			  if (client_conn == NULL) {
			    return ERR_MEM;
			  }
			  client_conn->conn_mode = hosted_if_CLIENT;
				  newpcb = tcp_new_ip_type(IP_GET_TYPE(remote_ip));
				  if (newpcb == NULL) {
					return ERR_MEM;
				  }

				  client_conn->raw_pcb = (void *)newpcb;
				  ip_addr_copy(client_conn->remote_ip, *remote_ip);
				  //client_conn->remote_ip = *remote_ip;
				  client_conn->remote_port = remote_port;
				  	if(IP_IS_V4_VAL(*remote_ip)){

				  		local_ip = *nt_dpm_get_ip(netif_get_by_index(DEFAULT_NETIF_IDX),IPADDR_TYPE_V4,0);

				  	}else{
				  		local_ip = *nt_dpm_get_ip(netif_get_by_index(DEFAULT_NETIF_IDX),IPADDR_TYPE_V6,0);
				  	}
				  	ip_addr_copy(newpcb->local_ip, local_ip);

				  tcp_arg(newpcb, client_conn);
				  tcp_sent(newpcb, nt_hosted_tcp_client_sent);
				  tcp_poll(newpcb, nt_hosted_tcp_poll, 2U);
				  tcp_err(newpcb, nt_hosted_tcp_err);


				  ip_addr_copy(remote_addr, *remote_ip);
				  err = tcp_connect(newpcb, &remote_addr, remote_port, nt_hosted_tcp_client_connected);
				  if (err != ERR_OK) {
					  nt_hosted_tcp_close(client_conn);
					return err;
				  }

				nt_hosted_conn_list_creat(&hosted_session_list, client_conn);
			  return err;


#endif

}


/** Receive data on an iperf tcp session */
static err_t
nt_hosted_tcp_recv(void *arg, struct tcp_pcb *tpcb, struct pbuf *p, err_t err)
{
  u8_t tmp;
  u16_t tot_len;
  u32_t packet_idx;
  struct pbuf *q;
  host_pbuf* host_pkt;
  hosted_session_cntxt*conn = (hosted_session_cntxt *)arg;

  LWIP_ASSERT("pcb mismatch", (struct tcp_pcb *)conn->conn_pcb == tpcb);
  LWIP_UNUSED_ARG(tpcb);

  if (err != ERR_OK) {
    nt_hosted_tcp_close(conn);
    return ERR_OK;
  }
  if (p == NULL) {
	  nt_hosted_tcp_close(conn);
    return ERR_OK;
  }
  tot_len = p->tot_len;
//  dbg_print_lwiperf_udp("tcp rcv tot_len", tot_len);
  host_pkt = (host_pbuf *)nt_osal_calloc(1,sizeof(host_pbuf));
  host_pkt->len = p->len;
  host_pkt->data = nt_osal_calloc(1,host_pkt->len);
  memscpy(host_pkt->data,host_pkt->len,p->payload,host_pkt->len);
  conn->poll_count = 0;

  BaseType_t xHigherPriorityTaskWoken = pdFALSE;
  is_traffic_runnig=TRUE;
  hosted_msg.device_action = send_data_to_host;
  hosted_msg.msg_struct.wapp_data = (void *)host_pkt;
  hosted_msg.msg_struct.result_function = &nt_hosted_result_fn;
  if(NT_QUEUE_FAIL == qurt_pipe_send_timed(x_spiQueueHandle,&hosted_msg,portMAX_DELAY))
  {
	  NT_LOG_COMMON_ERR("Queue send failed", 0, 0, 0);
  }
  if(nt_fail == nt_osal_semaphore_take(hosted_lwip_SemaphoreHandle, 1700))
  {
	  NT_LOG_WIFI_APP_INFO("Resuming tcp/ip task ", 0, 0, 0);
  }

  tcp_recved(tpcb, tot_len);
  pbuf_free(p);
  return ERR_OK;
}


static err_t
nt_hosted_tcp_accept(void *arg, struct tcp_pcb *newpcb, err_t err)
{
	hosted_session_cntxt *s,*conn;
  if ((err != ERR_OK) || (newpcb == NULL) || (arg == NULL)) {
    return ERR_VAL;
  }
#ifdef NT_DEBUG
  NT_LOG_DPM_INFO("tcp_accept", 0,0,0);
#endif
  s = (hosted_session_cntxt *)arg;
//  LWIP_ASSERT("invalid session", s->base.server);
  LWIP_ASSERT("invalid listen pcb", s->raw_pcb != NULL);
  LWIP_ASSERT("invalid conn pcb", s->conn_pcb == NULL);
  conn = (hosted_session_cntxt *)nt_osal_calloc(1,sizeof(hosted_session_cntxt));
  if (conn == NULL) {
    return ERR_MEM;
  }
  memset(conn, 0, sizeof(hosted_session_cntxt));
   conn->raw_pcb = s->raw_pcb;
   conn->conn_pcb = newpcb;
   conn->conn_mode = hosted_if_SERVER;
   if((hosted_lwip_SemaphoreHandle == NULL) )
   {
	   nt_osal_semaphore_create_binary(hosted_lwip_SemaphoreHandle);
	   if((hosted_lwip_SemaphoreHandle == NULL) )
	   {
		   NT_LOG_WIFI_APP_CRIT("Hosted Lwip Semaphore not created,System failure",0,0,0);
		   return ERR_VAL;
	   }
   }
   if(nt_fail == nt_osal_semaphore_take(hosted_lwip_SemaphoreHandle, portMAX_DELAY))
   {
	   NT_LOG_WIFI_APP_ERR("Semaphore take failed", 0, 0, 0);
   }


   /* setup the tcp rx connection */
   tcp_arg(newpcb, conn);
  tcp_recv(newpcb, nt_hosted_tcp_recv);
  tcp_poll(newpcb, nt_hosted_tcp_poll, 2U);
  tcp_err(newpcb, nt_hosted_tcp_err);

  return ERR_OK;
}
uint8_t nt_hosted_start_tcp_server(const char * local_addr,uint16_t local_port)
{
	  err_t err;
	  struct tcp_pcb *pcb;
	  hosted_session_cntxt  * cur_session;

	  ip_addr_t local_ip ; //*nt_dpm_get_ip(netif_get_by_index(DEFAULT_NETIF_IDX),IPADDR_TYPE_V4,0);
	  ipaddr_aton(local_addr,&local_ip);//*nt_dpm_get_ip(netif_get_by_index(DEFAULT_NETIF_IDX),IPADDR_TYPE_V4,0);

//	  LWIP_ASSERT_CORE_LOCKED();

	  cur_session = (hosted_session_cntxt  * ) nt_osal_calloc(1,sizeof(hosted_session_cntxt));
	  pcb = tcp_new_ip_type(HOSTED_SERVER_IP_TYPE);
	  if (pcb == NULL) {
		return ERR_MEM;
	  }
#ifdef NT_DEBUG
	  NT_LOG_DPM_INFO("LOCAL port",local_port,0,0);
#endif
	  err = tcp_bind(pcb, &local_ip, local_port);
	  if (err != ERR_OK) {
		return err;
	  }
	  cur_session->raw_pcb = tcp_listen_with_backlog(pcb, 1);
	  if (cur_session->raw_pcb == NULL) {
		if (pcb != NULL) {
		  tcp_close(pcb);
		}
		nt_osal_free_memory(cur_session);
		return ERR_MEM;
	  }
	  pcb = NULL;

	  tcp_arg(cur_session->raw_pcb, cur_session);
	  tcp_accept(cur_session->raw_pcb, nt_hosted_tcp_accept);

	  cur_session->local_port = local_port;
	  cur_session->conn_mode = hosted_if_SERVER;
	  cur_session->type = hosted_conn_TCP;
	  nt_hosted_conn_list_creat(&hosted_session_list,cur_session);
	  return err;
}


/**
 * @brief remove the node from the active connection list.
 *
 * @param phead Head for the list respective list.
 * @param pdelete Node to be deleted from respective list.
 */
void nt_hosted_list_delete(hosted_session_cntxt **phead, hosted_session_cntxt* pdelete)
{
	hosted_session_cntxt *plist = NULL;

	plist = *phead;
	if (plist != NULL){

		if (plist == pdelete){
			*phead = plist->pnext;
		} else {
			while (plist != NULL) {
				if (plist->pnext == pdelete){
					plist->pnext = pdelete->pnext;
					nt_osal_free_memory(pdelete);
				}
				plist = plist->pnext;
			}
		}
	}
}


/**
 * @brief find active connection handle with remote ip and remote port for UDP.
 *
 * @param remote_ip remote_ip in network byte order.
 * @param remote_port remote_port for the connection
 * @param pnode pointer to pointer for connection handle.
 * @return TRUE on success & FALSE on failure.
 */
NT_BOOL nt_hosted_find_connection_ip(ip_addr_t *remote_ip, uint16_t remote_port,hosted_session_cntxt **pnode)
{

	hosted_session_cntxt *plist = NULL;

	ip_addr_t ip_list;



	if (remote_ip == NULL)

		return FALSE;



	if (ip_addr_cmp(remote_ip , IP_ANY_TYPE) || (remote_port == 0))

		return FALSE;



	/*find the active connection node*/



	for (plist = hosted_session_list; plist != NULL; plist = plist->pnext){

		ip_addr_t remote_ip_inlist;

#ifdef sock

		inet_addr_to_ip4addr(&(remote_ip_inlist.u_addr.ip4),&(plist->saddr_dst.sin_addr));

		ip_addr_set(&ip_list, &remote_ip_inlist);

		if ((ip_addr_cmp(&ip_list, remote_ip)) &&

				(remote_port == plist->saddr_dst.sin_port)) {

			*pnode = plist;

			return TRUE;

		}

#else

		if ((ip_addr_cmp(&plist->remote_ip, remote_ip)) &&(remote_port == plist->remote_port)) {

			*pnode = plist;

			return TRUE;

		}

#endif//sock



	}

	return FALSE;
}

/** Try to send more data on an iperf tcp session */
err_t nt_hosted_tcp_client_send_packet(uint8_t *msg, uint16_t length,char * remote_addr, uint16_t remote_port)
{
	err_t err = NT_FAIL;
	hosted_session_cntxt * cur_session;

	ip_addr_t remote_ip;
	ipaddr_aton(remote_addr, &remote_ip);
	if(nt_hosted_find_connection_ip(&remote_ip, remote_port,&cur_session))
	{

		do {
			//     dbg_print_lwiperf("tcp_write", txlen);
			err = tcp_write((struct tcp_pcb *)cur_session->raw_pcb, msg, length, 0);
			if (err ==  ERR_MEM) {
				length /= 2;
			}
		} while ((err == ERR_MEM) && (length >= (TCP_MSS / 2)));

		err = tcp_output((struct tcp_pcb *)cur_session->raw_pcb);
	}

	return err;
}

/** Close an UDP session */
static void
nt_hosted_udp_close(hosted_session_cntxt *conn, err_t err)
{


	if (conn->raw_pcb != NULL) {
		udp_disconnect(conn->raw_pcb);
		udp_remove(conn->raw_pcb);
	}
	if(err != ERR_OK){
#ifdef NT_DEBUG
		NT_LOG_DPM_INFO("udp_close_error", err,0,0);
#endif
	}
#ifdef NT_DEBUG
	NT_LOG_DPM_INFO("udp_close", err,0,0);
#endif
}


/** Receive data on an iperf UDP session */
static void
nt_hosted_udp_server_recv(void *arg, struct udp_pcb *pcb, struct pbuf *pb,
		const ip_addr_t *addr, u16_t port)
{
	u32_t curr_time;
	char pbuf[100];
	hosted_session_cntxt *ser = (hosted_session_cntxt *)arg;

	//(void)pb;
	(void)addr;
	(void)port;

	LWIP_ASSERT("pcb mismatch", (struct udp_pcb *)ser->raw_pcb == pcb);

	if(ser->conn_mode == hosted_if_SERVER &&  (struct udp_pcb *)ser->raw_pcb == NULL){
		nt_dbg_print("Server UDP handler exist but corresponding PCB is NULL hence closing the connection.");
		nt_hosted_udp_close(ser, ERR_VAL);
	}

#ifdef NT_TST_TIME_STAMP_ENABLE
	if ((nt_dpm_tm.rx_stat[UDP_OUTPUT].valid == 0) && (*(uint32_t *)(((int *)pb->payload) + 1) == nt_dpm_tm.rx_marker)) {
		nt_dpm_tm.rx_stat[UDP_OUTPUT].value = nt_hal_get_curr_time();
		nt_dpm_tm.rx_stat[UDP_OUTPUT].valid = 1;
	}
#endif

	curr_time = nt_hal_get_curr_time();
	ser->poll_count = 0;
	//	UART_Send(pb->payload, pb->tot_len);
	//	UART_Send("\r\n", 2);

	if(*(int *)pb->payload & (int)(0x80)){
		/* checking if end of session packet received to terminate the session. */
		nt_hosted_udp_close(ser, ERR_VAL);
	}else
	{
		nt_at_ok_error(NT_AT_UDP_MSG_RCVD,0,NULL);
		char c ='>';
		nt_at_spi_send_of_len(&c,1);
		nt_at_spi_send_of_len(pb->payload,pb->tot_len);
		c ='<';
		nt_at_spi_send_of_len(&c,1);
	}
	pbuf_free(pb);
}

/**
 * @ingroup udp_perf_raw_client.c
 * Start a UDP iperf server on a specific IP address and port and listen for
 * incoming connections from UDP iperf clients.
 *
 * @param pointer to local IP address.
 * @param local port assigned for current session of server.
 * @param thread id to differentiate during parallel execution.
 * @param pointer to UDP session connection handler.
 *
 * @returns a connection handle on success; NULL on failure.
 */

err_t
nt_hosted_start_udp_server_wmm_impl(const ip_addr_t *local_addr, u16_t local_port,uint8_t tid)
{
	err_t err;
	struct udp_pcb *pcb;
	hosted_session_cntxt *s;
	char pbuf[100];

	LWIP_ASSERT_CORE_LOCKED();


	s = (hosted_session_cntxt *)nt_osal_calloc(1,sizeof(hosted_session_cntxt));
	if (s == NULL) {
		return ERR_MEM;
	}

	s->conn_mode = hosted_if_SERVER;
	s->tid = tid;

	pcb = udp_new_ip_type(IPADDR_TYPE_ANY);
	if (pcb == NULL) {
		nt_dbg_print("UDP_PCB Allocation failed");
		nt_osal_free_memory(s);
		return ERR_MEM;
	}

	pcb->local_ip = *local_addr;
	pcb->local_port = local_port;
	s->raw_pcb = (void *)pcb;
	if (pcb != NULL) {
		err = udp_bind(pcb, local_addr, local_port);
#ifdef NT_DEBUG
		NT_LOG_DPM_INFO("udp_bind", err,0,0);
#endif
		if (err != ERR_OK) {
			nt_hosted_udp_close(s, err);
			return err;
		}else if(err == ERR_USE){
#ifdef NT_DEBUG
			NT_LOG_DPM_INFO("port already in use",0,0,0);
#endif
		}else{
			udp_recv(pcb,nt_hosted_udp_server_recv,s);
		}
	}

	return ERR_OK;
}


/**
 * @ingroup udp_iperf_raw_client
 * Start a UDP iperf client to a specific IP address and port.
 *
 * @param pointer to remote IP address of server.
 * @param port number to connect on server.
 * @param thread id to differentiate during parallel execution.
 * @param buffer size to transmit.(Between 64 to 1800 bytes)
 * @param time in second to run the client.
 *
 * @returns a connection handle on success; NULL on failure.
 */
err_t nt_hosted_start_udp_wmm_client(const ip_addr_t *remote_ip,u16_t remote_port, uint8_t tid)
{
	err_t err;
	hosted_session_cntxt *client_conn;
	struct udp_pcb *newpcb;
	ip_addr_t local_ip;
	char pbuf[100];
	int i;

	LWIP_ASSERT("remote_ip != NULL", remote_ip != NULL);

	client_conn = (hosted_session_cntxt *)nt_osal_calloc(1,sizeof(hosted_session_cntxt));
	if (client_conn == NULL) {
		dbg_print_lwiperf_udp("UDP connection handler allocation failed",ERR_MEM);
		return ERR_MEM;
	}
	newpcb = udp_new_ip_type(IPADDR_TYPE_ANY);
	if (newpcb == NULL) {
		dbg_print_lwiperf_udp("UDP_PCB allocation failed",ERR_MEM);
		nt_osal_free_memory(client_conn);
		return ERR_MEM;
	}


	client_conn->conn_mode = hosted_if_SERVER;
	client_conn->raw_pcb = newpcb;
	client_conn->tid = tid;

	if(IP_IS_V4_VAL(*remote_ip)){
		local_ip = *nt_dpm_get_ip(netif_get_by_index(DEFAULT_NETIF_IDX),IPADDR_TYPE_V4,0);
	}else{
		local_ip = *nt_dpm_get_ip(netif_get_by_index(DEFAULT_NETIF_IDX),IPADDR_TYPE_V6,0);
	}

	newpcb->remote_ip = *remote_ip;
	newpcb->local_ip = local_ip;
	newpcb->tos = tid;
	newpcb->remote_port = remote_port;

	if (newpcb != NULL) {
		err = udp_bind(newpcb, &local_ip, 0);

		if (err == ERR_OK) {
			dbg_print_lwiperf_udp("udp_bind", err);
			err = udp_connect(newpcb,&newpcb->remote_ip, newpcb->remote_port);
			dbg_print_lwiperf_udp("udp_connected",err);
			if (err != ERR_OK) {
				nt_hosted_udp_close(client_conn, err);
				return err;
			}
			nt_dbg_print("----------------------------------------------------------------\r\n");
			snprintf(pbuf,sizeof(pbuf),"TID_C: %u Client connecting to %s, UDP port %u\r\n", tid, ipaddr_ntoa(&newpcb->remote_ip), newpcb->remote_port);
			nt_dbg_print(pbuf);
			nt_dbg_print("----------------------------------------------------------------\r\n");
		} else {
			nt_dbg_print("Failed to bind\r\n");
			nt_hosted_udp_close(client_conn, err);
		}
	}
	nt_hosted_conn_list_creat(&hosted_session_list, client_conn);
	return ERR_OK;
}

/**
 * @brief insert the node to the active connection list
 *
 * @param phead Head of the list.
 * @param pinsert Node to be insert in the list.
 */
void nt_hosted_conn_list_creat(hosted_session_cntxt **phead, hosted_session_cntxt *pinsert)
{
	hosted_session_cntxt *plist = NULL;
	if (*phead == NULL)
	{
		*phead = pinsert;
	}
	else {
		plist = *phead;
		while (plist->pnext != NULL) {
			plist = plist->pnext;
		}
		plist->pnext = pinsert;
	}
	pinsert->pnext = NULL;


}

/**
 * @brief Close connection for TCP/UDP using link_id.
 *
 * @param link_id ID of the link to be deleted.
 * @return
 */
uint8_t nt_hosted_conn_close(protocol_type type,ip_addr_t *remote_ip,uint16_t remote_port){

	hosted_session_cntxt *pnode;

	uint8_t ret = NT_FAIL;

	err_t send_err = 0;


	uint8_t is_conn = nt_hosted_find_connection_ip(remote_ip, remote_port,&pnode);

	struct pbuf *hosted_pb = pbuf_alloc(PBUF_TRANSPORT, 10, PBUF_RAM);
		if (NULL == hosted_pb) {
				nt_dbg_print("PBUFF allocation failed memory unavailable\r\n");
				 nt_hosted_udp_close(pnode,ret);
			}
		hosted_pb->type_internal |= PBUF_TYPE_FLAG_DATA_VOLATILE;
	if(is_conn ){
	#ifdef sock

	ret = close(pnode->sock);

	#else
	if(type ==hosted_conn_UDP )
	{

		int packid = -1;

		uint32_t *payload;
		hosted_pb->len = hosted_pb->tot_len = 10;
		payload = (uint32_t *) (hosted_pb->payload);

		*payload= htonl(packid);
		*(payload + 1) = 0;
		char* ip_addr = ipaddr_ntoa(remote_ip);
		for(int i=0;i<20;i++){
			do {
							if ((uint32_t)payload != (uint32_t)hosted_pb->payload) {
								if ((uint32_t)payload > (uint32_t)hosted_pb->payload) {
									pbuf_remove_header(hosted_pb, (uint32_t)payload - (uint32_t)hosted_pb->payload);
								} else {
									dbg_print_lwiperf_udp("payload error", (uint32_t)hosted_pb->payload);
								}

								if ((uint32_t)payload != (uint32_t)hosted_pb->payload) {
									dbg_print_lwiperf_udp("payload mismatch", (uint32_t)payload);
								}
							}
							LOCK_TCPIP_CORE();
							send_err = udp_send((struct udp_pcb *)pnode->raw_pcb, hosted_pb);
							UNLOCK_TCPIP_CORE();

						}while(send_err == ERR_MEM);
		}
	      nt_hosted_udp_close(pnode,send_err);
	      pbuf_free(hosted_pb);
		}else if(type==hosted_conn_TCP){
		tcp_close(pnode->raw_pcb);
	}


	#endif //sock



	nt_hosted_list_delete(&hosted_session_list, pnode);

	} else {
#ifdef NT_DEBUG
		NT_LOG_DPM_INFO("No connection found with link_id or IP",0,0,0);
#endif
	}

	return NT_OK;
}

void nt_hosted_push_pkt_to_host(host_pbuf *p,void (*resp_function)())
{
	//	taskENTER_CRITICAL();
	if(*(int *)p->data == htonl(-1))
		{
				nt_at_ok_error(NT_AT_SERVER_CLOSED,0,NULL);
		}else{
			nt_at_ok_error(NT_AT_TCP_MSG_RCVD,0,NULL);
						char c ='>';
						nt_at_spi_send_of_len(&c,1);
						nt_at_spi_send_of_len(p->data,p->len);
						c ='<';
						nt_at_spi_send_of_len(&c,1);
						//	nt_at_ok_error(NT_AT_TCP_MSG_RCVD_COM,0,NULL);
						//	taskEXIT_CRITICAL();
		}

	nt_osal_free_memory(p->data);
	nt_osal_free_memory(p);

	resp_function();
}

#endif//#ifdef NT_HOSTED_SDK

