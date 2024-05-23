/*
 * Copyright (c) 2024 Qualcomm Innovation Center, Inc. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

/*
 * SPDX-FileCopyrightText: 2015-2023 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "lwip/apps/nt_dhcps.h"
#include <assert.h>
#include "safeAPI.h"

#if LWIP_IPV4 && NT_FN_DHCPS_V4

#include "lwip/inet.h"
#include "lwip/err.h"
#include "lwip/pbuf.h"
#include "lwip/udp.h"
#include "lwip/mem.h"
#include "lwip/prot/iana.h"

#include "network_al.h"
#include "nt_logger_api.h"

#define DHCPS_LEASE_TIME_DEF	(120)
struct dhcps_lease dhcps_lease;
list_node *plist = NULL;
dhcps_arg_t *dhcp_config = NULL;
#ifdef NT_FN_USE_DNS
static ip_addr_t ipadd_dhcp_dns_s= {0};
#endif // NT_FN_USE_DNS


#ifdef NT_FN_USE_DNS
void nt_set_dhcp_dns_s(ip_addr_t ipaddr)
{
//	ipadd_dhcp_dns_s=ipaddr;
	ip_addr_set(&ipadd_dhcp_dns_s,&ipaddr);
}

ip_addr_t * dhcp_dns_getserver()
{
	return &ipadd_dhcp_dns_s;
}
#endif // NT_FN_USE_DNS
/**
 * @ingroup nt_dhcps
 * insert the node to the list.
 *
 * @param Head of the linked list.
 * @param pointer to Node to be insert.
 *
 * @return None
 */
void nt_dhcps_node_insert_to_list(list_node **phead, list_node* pinsert)
{
	list_node *plist = NULL;
	struct dhcps_pool *pdhcps_pool = NULL;
	struct dhcps_pool *pdhcps_node = NULL;
	if (*phead == NULL)
		*phead = pinsert;
	else {
		plist = *phead;
		pdhcps_node = pinsert->pnode;
		pdhcps_pool = plist->pnode;

		if(ip_2_ip4(&pdhcps_node->ip)->addr < ip_2_ip4(&pdhcps_pool->ip)->addr) {
			pinsert->pnext = plist;
			*phead = pinsert;
		} else {
			while (plist->pnext != NULL) {
				pdhcps_pool = plist->pnext->pnode;
				if (ip_2_ip4(&pdhcps_node->ip)->addr < ip_2_ip4(&pdhcps_pool->ip)->addr) {
					pinsert->pnext = plist->pnext;
					plist->pnext = pinsert;
					break;
				}
				plist = plist->pnext;
			}

			if(plist->pnext == NULL) {
				plist->pnext = pinsert;
			}
		}
	}
}

/**
 * @ingroup nt_dhcps
 * remove the node from list.
 *
 * @param Head of the linked list.
 * @param Node to be delete.
 *
 * @return None
 */
void nt_dhcps_node_remove_from_list(list_node **phead, list_node* pdelete)
{
	list_node *plist = NULL;;

	plist = *phead;
	if (plist == NULL){
		*phead = NULL;
	} else {
		if (plist == pdelete){
			*phead = plist->pnext;
			pdelete->pnext = NULL;
		} else {
			while (plist != NULL) {
				if (plist->pnext == pdelete){
					plist->pnext = pdelete->pnext;
					pdelete->pnext = NULL;
				}
				plist = plist->pnext;
			}
		}
	}
}

/**
 * @ingroup nt_dhcps
 * Set option in dhcps_msg to DHCP_OPTION_MESSAGE_TYPE specific
 * type of msg (DHCPOFFER, DHCPNAK, DHCPACK etc)
 *
 * @param pointer to option field in dhcps_msg.
 * @param Specifice msg type.
 *
 * @return pointer to next option field.
 */
static uint8_t* nt_dhcps_add_msg_type(uint8_t *optptr, uint8_t type)
{

	*optptr++ = DHCP_OPTION_MESSAGE_TYPE;
	*optptr++ = 1;
	*optptr++ = type;
	return optptr;
}

/**
 * @ingroup nt_dhcps
 * Add options to dhcp msg.
 *
 * @param pointer to option field in dhcps_msg.
 *
 * @return pointer to next options.
 */
static uint8_t* nt_dhcps_add_offer_options(uint8_t *optptr)
{
	ip_addr_t ipadd = {0};

	ip_2_ip4(&ipadd)->addr = ip_2_ip4(&dhcp_config->dhcps_addr)->addr;

#ifdef USE_CLASS_B_NET
	*optptr++ = DHCP_OPTION_SUBNET_MASK;
	*optptr++ = 4;  //length
	*optptr++ = 255;
	*optptr++ = 240;
	*optptr++ = 0;
	*optptr++ = 0;
#else
	*optptr++ = DHCP_OPTION_SUBNET_MASK;
	*optptr++ = 4;
	*optptr++ = 255;
	*optptr++ = 255;
	*optptr++ = 255;
	*optptr++ = 0;
#endif

	*optptr++ = DHCP_OPTION_LEASE_TIME;
	*optptr++ = 4;
	*optptr++ = ((DHCPS_LEASE_TIMER * 60) >> 24) & 0xFF;
	*optptr++ = ((DHCPS_LEASE_TIMER * 60) >> 16) & 0xFF;
	*optptr++ = ((DHCPS_LEASE_TIMER * 60) >> 8) & 0xFF;
	*optptr++ = ((DHCPS_LEASE_TIMER * 60) >> 0) & 0xFF;

	*optptr++ = DHCP_OPTION_SERVER_ID;
	*optptr++ = 4;
	*optptr++ = ip4_addr1( ip_2_ip4(&ipadd));
	*optptr++ = ip4_addr2( ip_2_ip4(&ipadd));
	*optptr++ = ip4_addr3( ip_2_ip4(&ipadd));
	*optptr++ = ip4_addr4( ip_2_ip4(&ipadd));

	if (dhcps_router_enabled(dhcp_config->offer)){
		struct ip_info if_ip;
		memset(&if_ip, 0x0, sizeof(struct ip_info));
		nt_dpm_get_ip_info(nt_get_netifidx_by_devmode(AP_DEVICE), &if_ip);

		*optptr++ = DHCP_OPTION_ROUTER;
		*optptr++ = 4;
		*optptr++ = ip4_addr1( ip_2_ip4(&if_ip.gw));
		*optptr++ = ip4_addr2( ip_2_ip4(&if_ip.gw));
		*optptr++ = ip4_addr3( ip_2_ip4(&if_ip.gw));
		*optptr++ = ip4_addr4( ip_2_ip4(&if_ip.gw));
	}

#ifdef NT_FN_USE_DNS
	*optptr++ = DHCP_OPTION_DNS_SERVER;
	*optptr++ = 4;
	*optptr++ = ip4_addr1( ip_2_ip4(&ipadd_dhcp_dns_s));
	*optptr++ = ip4_addr2( ip_2_ip4(&ipadd_dhcp_dns_s));
	*optptr++ = ip4_addr3( ip_2_ip4(&ipadd_dhcp_dns_s));
	*optptr++ = ip4_addr4( ip_2_ip4(&ipadd_dhcp_dns_s));
#endif

#ifdef CLASS_B_NET
	*optptr++ = DHCP_OPTION_BROADCAST;
	*optptr++ = 4;
	*optptr++ = ip4_addr1( &ipadd);
	*optptr++ = 255;
	*optptr++ = 255;
	*optptr++ = 255;
#else
	*optptr++ = DHCP_OPTION_BROADCAST;
	*optptr++ = 4;
	*optptr++ = ip4_addr1( ip_2_ip4(&ipadd));
	*optptr++ = ip4_addr2( ip_2_ip4(&ipadd));
	*optptr++ = ip4_addr3( ip_2_ip4(&ipadd));
	*optptr++ = 255;
#endif

	*optptr++ = DHCP_OPTION_MTU;
	*optptr++ = 2;
#ifdef CLASS_B_NET
	*optptr++ = 0x05;
	*optptr++ = 0xdc;
#else
	*optptr++ = 0x02;
	*optptr++ = 0x40;
#endif

	*optptr++ = DHCP_OPTION_PERFORM_ROUTER_DISCOVERY;
	*optptr++ = 1;
	*optptr++ = 0x00;

	*optptr++ = 43;
	*optptr++ = 6;

	*optptr++ = 0x01;
	*optptr++ = 4;
	*optptr++ = 0x00;
	*optptr++ = 0x00;
	*optptr++ = 0x00;
	*optptr++ = 0x02;

	return optptr;
}

/**
 * @ingroup nt_dhcps
 * Add END (DHCP_OPTION_END) options to dhcp msg.
 *
 * @param pointer to option field in dhcps_msg.
 *
 * @return pointer to next options.
 */
static uint8_t* nt_dhcps_add_end(uint8_t *optptr)
{
	*optptr++ = DHCP_OPTION_END;
	return optptr;
}

/**
 * @ingroup nt_dhcps
 * Create a DHCP response, fill in common headers
 *
 * @param dhcps_msg struct pointer
 */
static void nt_dhcps_create_msg(struct dhcps_msg *m)
{
	ip_addr_t client;

	ip_2_ip4(&client)->addr = ip_2_ip4(&dhcp_config->dhcpc_addr)->addr;

	m->op = DHCP_BOOTREPLY;
	m->htype = LWIP_IANA_HWTYPE_ETHERNET;
	m->hlen = NT_MAC_ADDR_SIZE;
	m->hops = 0;
	m->secs = 0;
	m->flags = htons(BOOTP_BROADCAST);

	memscpy((char *) m->yiaddr, sizeof(m->yiaddr), (char *) &ip_2_ip4(&client)->addr, sizeof(m->yiaddr));

	uint32_t magic_cookie1 = PP_HTONL(DHCP_MAGIC_COOKIE);
	memscpy((char *) m->options, sizeof(magic_cookie1), &magic_cookie1, sizeof(magic_cookie1));
}

/**
 * @ingroup nt_dhcps
 * Pbuff allocation for dhcp_msg.
 *
 * @param len of pbuf to be allocated.
 *
 * @return pointer to pbuf.
 */
struct pbuf * nt_dhcps_pbuf_alloc(u16_t len)
{
	u16_t mlen = sizeof(struct dhcps_msg);

	if (len > mlen) {
		mlen = len;
	}

	return pbuf_alloc(PBUF_TRANSPORT, mlen, PBUF_RAM);
}

/**
 * @ingroup nt_dhcps
 * Create and send offer to the dhcp client.
 *
 * @param pointer to netif on which dhcp is configured.
 * @param pointer to dhcp msg.
 * @param length of dhcp msg.
 *
 * @return none.
 */
static void nt_dhcps_send_offer(struct netif *netif,struct dhcps_msg *m, u16_t len)
{
	uint8_t *end;
	struct pbuf *p, *q;
	uint8_t *data;
	u16_t cnt=0;
	u16_t i;
	err_t SendOffer_err_t;
	nt_dhcps_create_msg(m);

	end = nt_dhcps_add_msg_type(&m->options[4], DHCP_OFFER);
	end = nt_dhcps_add_offer_options(end);
	end = nt_dhcps_add_end(end);

	p = nt_dhcps_pbuf_alloc(len);
	if(p != NULL){
		q = p;
		while(q != NULL){
			data = (uint8_t *)q->payload;
			for(i=0; i<q->len; i++)
			{
				data[i] = ((uint8_t *) m)[cnt++];
			}

			q = q->next;
		}
	}else{
		NT_LOG_PRINT(COMMON,ERR,"nt_dhcps_send_offer pbuf_alloc failed");
		return;
	}
	SendOffer_err_t = udp_sendto( netif->dhcps_pcb, p, &ip_addr_broadcast, NT_PORT_DHCP_CLIENT );

	if(SendOffer_err_t != ERR_OK){
		NT_LOG_PRINT(COMMON,ERR,"nt_dhcps_send_offer send failed %d",SendOffer_err_t);
	}

	if(p->ref != 0){
		pbuf_free(p);
	}
}

/**
 * @ingroup nt_dhcps
 * Create and send DHCP_NAK to the dhcp client.
 *
 * @param pointer to netif on which dhcp is configured.
 * @param pointer to dhcp msg.
 * @param length of dhcp msg.
 *
 * @return none.
 */
static void nt_dhcps_send_nak(struct netif *netif, struct dhcps_msg *m, u16_t len)
{

	uint8_t *end;
	struct pbuf *p, *q;
	uint8_t *data;
	u16_t cnt=0;
	u16_t i;
	err_t SendNak_err_t;
	nt_dhcps_create_msg(m);

	end = nt_dhcps_add_msg_type(&m->options[4], DHCP_NAK);
	end = nt_dhcps_add_end(end);

	p = nt_dhcps_pbuf_alloc(len);

	if(p != NULL){
		q = p;
		while(q != NULL){
			data = (uint8_t *)q->payload;
			for(i=0; i<q->len; i++)
			{
				data[i] = ((uint8_t *) m)[cnt++];
			}

			q = q->next;
		}
	}else{
		NT_LOG_PRINT(COMMON,ERR,"nt_dhcps_send_nak pbuf_alloc failed");
		return;
	}
	SendNak_err_t = udp_sendto(netif->dhcps_pcb, p, &ip_addr_broadcast, NT_PORT_DHCP_CLIENT );

	if(SendNak_err_t != ERR_OK){
			NT_LOG_PRINT(COMMON,ERR,"nt_dhcps_send_nak send failed %d",SendNak_err_t);
		}

	if(p->ref != 0){
		pbuf_free(p);
	}
}

/**
 * @ingroup nt_dhcps
 * Create and send DHCP_ACK to the dhcp client.
 *
 * @param pointer to netif on which dhcp is configured.
 * @param pointer to dhcp msg.
 * @param length of dhcp msg.
 *
 * @return none.
 */
static void nt_dhcps_send_ack(struct netif *netif, struct dhcps_msg *m, u16_t len)
{

	uint8_t *end;
	struct pbuf *p, *q;
	uint8_t *data;
	u16_t cnt=0;
	u16_t i;
	err_t SendAck_err_t;
	nt_dhcps_create_msg(m);

	end = nt_dhcps_add_msg_type(&m->options[4], DHCP_ACK);
	end = nt_dhcps_add_offer_options(end);
	end = nt_dhcps_add_end(end);

	p = nt_dhcps_pbuf_alloc(len);
	if(p != NULL){
		q = p;
		while(q != NULL){
			data = (uint8_t *)q->payload;
			for(i=0; i<q->len; i++)
			{
				data[i] = ((uint8_t *) m)[cnt++];
			}

			q = q->next;
		}
	}else{
		NT_LOG_PRINT(COMMON,ERR,"nt_dhcps_send_ack pbuf_alloc failed");
		return;
	}
	SendAck_err_t = udp_sendto( netif->dhcps_pcb, p, &ip_addr_broadcast, NT_PORT_DHCP_CLIENT );

	if(SendAck_err_t != ERR_OK){
		NT_LOG_PRINT(COMMON,ERR,"nt_dhcps_send_ack send failed %d",SendAck_err_t);
	}

	if(p->ref != 0){
		pbuf_free(p);
	}
}

/**
 * @ingroup nt_dhcps
 * Parse options from dhcp_msg  received through dhcp recv.
 *
 * @param pointer to option field in dhcp msg.
 * @param length of dhcp msg.
 *
 * @return state of dhcp server.
 */
static uint8_t nt_dhcps_parse_options(uint8_t *optptr, int16_t len)
{
	ip_addr_t client = {0};
	NT_BOOL is_dhcp_parse_end = FALSE;
	struct dhcps_state s;

	ip_2_ip4(&client)->addr = ip_2_ip4(&dhcp_config->dhcpc_addr)->addr;

	uint8_t *end = optptr + len;
	u16_t type = 0;

	s.state = DHCPS_STATE_IDLE;

	while (optptr < end) {
		switch ((int16_t) *optptr) {

		case DHCP_OPTION_MESSAGE_TYPE:	//53
			type = *(optptr + 2);
			NT_LOG_PRINT(COMMON,INFO,"parse option DHCP_OPTION_MESSAGE_TYPE");
			break;

		case DHCP_OPTION_REQUESTED_IP://50
			if( memcmp( (char *) &ip_2_ip4(&client)->addr, (char *) optptr+2,4)==0 ) {
				NT_LOG_PRINT(COMMON,INFO,"DHCP_OPTION_REQUESTED_IP = 0 ok");
				s.state = DHCPS_STATE_ACK;
			}else {
				NT_LOG_PRINT(COMMON,INFO,"DHCP_OPTION_REQUESTED_IP != 0 err");
				s.state = DHCPS_STATE_NAK;
			}
			break;
		case DHCP_OPTION_END:
		{
			is_dhcp_parse_end = TRUE;
		}
		break;
		}

		if(is_dhcp_parse_end){
			break;
		}

		optptr += optptr[1] + 2;
	}

	switch (type){

	case DHCP_DISCOVER://1
		s.state = DHCPS_STATE_OFFER;
		NT_LOG_PRINT(COMMON,INFO,"DHCPD_STATE_OFFER");
		break;

	case DHCP_REQUEST://3
		if ( !(s.state == DHCPS_STATE_ACK || s.state == DHCPS_STATE_NAK) ) {
			if(dhcp_config->renew == TRUE) {
				s.state = DHCPS_STATE_ACK;
				NT_LOG_PRINT(COMMON,INFO,"DHCPS_STATE_ACK");
			} else {
				s.state = DHCPS_STATE_NAK;
				NT_LOG_PRINT(COMMON,INFO,"DHCPD_STATE_NAK");
			}
		}
		break;

	case DHCP_DECLINE://4
		s.state = DHCPS_STATE_IDLE;
		NT_LOG_PRINT(COMMON,INFO,"DHCPD_STATE_IDLE");
		break;

	case DHCP_RELEASE://7
		s.state = DHCPS_STATE_RELEASE;
		NT_LOG_PRINT(COMMON,INFO,"DHCPS_STATE_RELEASE");
		break;
	}
	return s.state;
}

/**
 * @ingroup nt_dhcps
 * Parse dhcp_msg  received through dhcp recv.
 *
 * @param pointer to dhcp msg.
 * @param length of dhcp msg.
 *
 * @return state of dhcp server.
 */
static int8_t nt_dhcps_parse_msg(struct dhcps_msg *m, u16_t len)
{
	uint32_t magic_cookie = PP_HTONL(DHCP_MAGIC_COOKIE);
	if(memcmp((char *)m->options, &magic_cookie, sizeof(magic_cookie)) == 0){
		ip_addr_t ip;
		memscpy(&ip_2_ip4(&ip)->addr,sizeof(ip_2_ip4(&ip)->addr),m->ciaddr,sizeof(ip_2_ip4(&ip)->addr));
		ip_2_ip4(&dhcp_config->dhcpc_addr)->addr = nt_dhcps_client_update(m->chaddr,&ip);

		int8_t ret = nt_dhcps_parse_options(&m->options[4], len);

		if(ret == DHCPS_STATE_RELEASE) {
			nt_dhcps_client_leave(m->chaddr,&ip,TRUE); // force to delete
			ip_2_ip4(&dhcp_config->dhcpc_addr)->addr = ip_2_ip4(&ip)->addr;
		}
		return ret;
	}
	return 0;
}


/**
 * @ingroup nt_dhcps
 * dhcp receive callback.
 */
static void nt_dhcps_cb(void *arg, struct udp_pcb *pcb, struct pbuf *p,
		ip_addr_t *addr, uint16_t port)
{
	struct dhcps_msg *pmsg_dhcps = NULL;
	int16_t tlen = 0, malloc_len;
	u16_t i = 0;
	u16_t dhcps_msg_cnt = 0;
	uint8_t *p_dhcps_msg = NULL;
	uint8_t *data = NULL;
	dhcps_arg_t * dhcp_conf= NULL;

#if DHCPS_DEBUG
	NT_LOG_PRINT(COMMON,INFO,"nt_dhcps_cb-> receive a packet");
#endif
	if(arg == NULL){
		NT_LOG_PRINT(COMMON,INFO,"nt_dhcps_cb arg == NULL");
		return;
	}

	dhcp_conf = (dhcps_arg_t *)arg;

	if (p == NULL){
		NT_LOG_PRINT(COMMON,INFO,"nt_dhcps_cb pbuf == NULL");
		return;
	}

	malloc_len = sizeof(struct dhcps_msg);

	if (malloc_len < p->tot_len) {
		malloc_len = p->tot_len;
	}

	pmsg_dhcps = (struct dhcps_msg *)nt_osal_calloc(1,malloc_len);
	if (NULL == pmsg_dhcps){
		pbuf_free(p);
		return;
	}


	p_dhcps_msg = (uint8_t *)pmsg_dhcps;
	tlen = p->tot_len;
	data = p->payload;

	for(i=0; i<p->len; i++){
		p_dhcps_msg[dhcps_msg_cnt++] = data[i];
	}

	if(p->next != NULL) {
		data = p->next->payload;
		for(i=0; i<p->next->len; i++){
			p_dhcps_msg[dhcps_msg_cnt++] = data[i];
		}
	}

#if DHCPS_DEBUG
	NT_LOG_PRINT(COMMON,INFO,"nt_dhcps_cb-> nt_dhcps_parse_msg");
#endif

	switch(nt_dhcps_parse_msg(pmsg_dhcps, tlen - 240)) {

	case DHCPS_STATE_OFFER://1
#if DHCPS_DEBUG
		NT_LOG_PRINT(COMMON,INFO,"nt_dhcps_cb-> DHCPD_STATE_OFFER");
#endif
		nt_dhcps_send_offer(dhcp_conf->netif, pmsg_dhcps, malloc_len);
		break;
	case DHCPS_STATE_ACK://3
#if DHCPS_DEBUG
		NT_LOG_PRINT(COMMON,INFO,"nt_dhcps_cb-> DHCPD_STATE_ACK");
#endif
		nt_dhcps_send_ack(dhcp_conf->netif, pmsg_dhcps, malloc_len);
		//wifi_softap_set_station_info(pmsg_dhcps->chaddr, &ip_2_ip4(&client_address)->addr);
		break;
	case DHCPS_STATE_NAK://4
#if DHCPS_DEBUG
		NT_LOG_PRINT(COMMON,INFO,"dhcps: nt_dhcps_cb-> DHCPD_STATE_NAK");
#endif
		nt_dhcps_send_nak(dhcp_conf->netif, pmsg_dhcps, malloc_len);
		break;
	default :
		break;
	}

	pbuf_free(p);
	nt_osal_free_memory(pmsg_dhcps);
	pmsg_dhcps = NULL;
	dhcp_conf = NULL;
}

/**
 * @ingroup nt_dhcps
 * Initialize the DHCP POOL IP address.
 *
 * @param ip IP address of the AP.
 */

static void nt_init_dhcps_lease(uint32_t ip)
{
	uint32_t softap_ip = 0,local_ip = 0;
	uint32_t start_ip = 0;
	uint32_t end_ip = 0;

	if (dhcps_lease.enable == TRUE) {
		softap_ip = htonl(ip);
		start_ip = htonl(ip_2_ip4(&dhcps_lease.start_ip)->addr);
		end_ip = htonl(ip_2_ip4(&dhcps_lease.end_ip)->addr);
		/*config ip information can't contain local ip*/
		if ((start_ip <= softap_ip) && (softap_ip <= end_ip)) {
			dhcps_lease.enable = FALSE;
		} else {
			/*config ip information must be in the same segment as the local ip*/
			softap_ip >>= 8;
			if (((start_ip >> 8 != softap_ip) || (end_ip >> 8 != softap_ip))
					|| (end_ip - start_ip > DHCPS_MAX_LEASE)) {
				dhcps_lease.enable = FALSE;
			}
		}
	}

	if (dhcps_lease.enable == FALSE) {
		local_ip = softap_ip = htonl(ip);
		softap_ip &= 0xFFFFFF00;
		local_ip &= 0xFF;
		if (local_ip >= 0x80)
			local_ip -= DHCPS_MAX_LEASE;
		else
			local_ip++;

		memset(&dhcps_lease, 0x0, sizeof(dhcps_lease));
		ip_2_ip4(&dhcps_lease.start_ip)->addr = softap_ip | local_ip;
		ip_2_ip4(&dhcps_lease.end_ip)->addr = softap_ip | (local_ip + DHCPS_MAX_LEASE - 1);
		ip_2_ip4(&dhcps_lease.start_ip)->addr = htonl(ip_2_ip4(&dhcps_lease.start_ip)->addr);
		ip_2_ip4(&dhcps_lease.end_ip)->addr= htonl(ip_2_ip4(&dhcps_lease.end_ip)->addr);
	}
	NT_LOG_PRINT(COMMON,INFO,"start_ip = %s,", ipaddr_ntoa((const ip_addr_t*)&dhcps_lease.start_ip));
	NT_LOG_PRINT(COMMON,INFO,"end_ip = %s", ipaddr_ntoa((const ip_addr_t*)&dhcps_lease.end_ip));
}

/**
 * @ingroup nt_dhcps
 * start DHCP server on default network interface
 *
 * @param pointer to network interface
 * @param ip IP address pointer to hold the ip address value.
 */
void nt_dhcps_start(struct netif *netif, struct ip_info *info)
{
	struct udp_pcb *pcb_dhcps = NULL;
	err_t err;

	if(netif == NULL){
		NT_LOG_PRINT(COMMON,ERR,"nt_dhcps_start: netif == NULL");
		return;
	}

	if(netif->dhcps_pcb != NULL) {
		udp_remove(netif->dhcps_pcb);
	}

	dhcp_config = nt_osal_calloc(1,sizeof(dhcps_arg_t));

	if(dhcp_config == NULL){
		NT_LOG_PRINT(COMMON,ERR,"nt_dhcps_start: apnetif == NULL");
		return;
	}

	if(dhcps_lease.lease_time != 0){
		dhcp_config->dhcps_lease_time = dhcps_lease.lease_time;
	} else {
		dhcp_config->dhcps_lease_time = DHCPS_LEASE_TIME_DEF;
	}
	dhcp_config->renew = FALSE;
	dhcp_config->offer = 0xFF;
	dhcp_config->netif = netif;

	pcb_dhcps = udp_new();
	if (pcb_dhcps == NULL || info == NULL) {
		NT_LOG_PRINT(COMMON,ERR,"nt_dhcps_start: could not obtain pcb");
		goto exit;
	}

	dhcp_config->pcb_dhcps = netif->dhcps_pcb = pcb_dhcps;
	dhcp_config->dhcps_addr = info->ip;

	pcb_dhcps->netif_idx =  nt_get_netifidx_by_devmode(AP_DEVICE);
	nt_init_dhcps_lease(ip_2_ip4(&dhcp_config->dhcps_addr)->addr);

	err = udp_bind(pcb_dhcps, IP_ADDR_ANY, NT_PORT_DHCP_SERVER);
	if(err != ERR_OK){
		NT_LOG_PRINT(COMMON,ERR,"nt_dhcps_start: Bind failed %d", err);
		udp_remove(netif->dhcps_pcb);
		goto exit;
	}
	udp_recv(pcb_dhcps, nt_dhcps_cb, dhcp_config);
	return;

	exit:
	nt_osal_free_memory(dhcp_config);
	dhcp_config = NULL;
	return;
}

/**
 * @ingroup nt_dhcps
 * stop DHCP server on default network interface.
 *
 *  @param pointer to network interface
 */
void nt_dhcps_stop(struct netif * netif)
{
	if(netif == NULL){
		NT_LOG_PRINT(COMMON,ERR,"nt_dhcps_stop: netif == NULL");
		return;
	}

	if(netif->dhcps_pcb != NULL) {
		udp_disconnect(netif->dhcps_pcb);
	}else return;

	if(netif->dhcps_pcb != NULL) {
		udp_remove(netif->dhcps_pcb);
		netif->dhcps_pcb = NULL;
	}

	list_node *pnode = NULL;
	list_node *pback_node = NULL;
	struct dhcps_pool* dhcp_node = NULL;

	pnode = plist;
	while (pnode != NULL) {
		pback_node = pnode;
		pnode = pback_node->pnext;
		nt_dhcps_node_remove_from_list(&plist, pback_node);
		dhcp_node = (struct dhcps_pool*)pback_node->pnode;
		nt_dhcps_client_leave(dhcp_node->mac,&dhcp_node->ip,TRUE); // force to delete
		//wifi_softap_set_station_info(dhcp_node->mac, &ip_zero);
		nt_osal_free_memory(pback_node->pnode);
		pback_node->pnode = NULL;
		nt_osal_free_memory(pback_node);
		pback_node = NULL;
	}

	if(dhcp_config != NULL){
		nt_osal_free_memory(dhcp_config);
		dhcp_config = NULL;
	}
}

/**
 * @ingroup nt_dhcps
 * set the lease information of DHCP server.
 *
 * @param  pointer to dhcps_lease.
 * @return TRUE or FALSE
 */
NT_BOOL nt_set_dhcps_lease(struct dhcps_lease *please)
{
	struct ip_info info;
	uint32_t softap_ip = 0;
	uint32_t start_ip = 0;
	uint32_t end_ip = 0;

	uint8_t opmode = nt_get_opmode();

	if (opmode == STA_MODE || opmode == NT_EFAIL) {
		return FALSE;
	}

	if (please == NULL || nt_dhcps_status() == DHCP_STARTED)
		return FALSE;

	if(please->enable) {
		memset(&info, 0x0, sizeof(struct ip_info));
		nt_dpm_get_ip_info(nt_get_netifidx_by_devmode(AP_DEVICE), &info);
		softap_ip = htonl(ip_2_ip4(&info.ip)->addr);
		start_ip = htonl(ip_2_ip4(&please->start_ip)->addr);
		end_ip = htonl(ip_2_ip4(&please->end_ip)->addr);

		/*config ip information can't contain local ip*/
		if ((start_ip <= softap_ip) && (softap_ip <= end_ip))
			return FALSE;

		/*config ip information must be in the same segment as the local ip*/
		softap_ip >>= 8;
		if ((start_ip >> 8 != softap_ip)
				|| (end_ip >> 8 != softap_ip)) {
			return FALSE;
		}

		if (end_ip - start_ip > DHCPS_MAX_LEASE)
			return FALSE;

		memset(&dhcps_lease, 0x0, sizeof(dhcps_lease));
		ip_2_ip4(&dhcps_lease.start_ip)->addr = ip_2_ip4(&please->start_ip)->addr;
		ip_2_ip4(&dhcps_lease.end_ip)->addr = ip_2_ip4(&please->end_ip)->addr;
		dhcps_lease.lease_time = please->lease_time;
	}
	dhcps_lease.enable = please->enable;
	//	dhcps_lease_flag = FALSE;
	return TRUE;
}

/**
 * @ingroup nt_dhcps
 * get the lease information of DHCP server.
 *
 * @param  pointer to dhcps_lease.
 * @return TRUE or FALSE
 */
NT_BOOL nt_get_dhcps_lease(struct dhcps_lease *please)
{
	uint8_t opmode = nt_get_opmode();

	if (opmode == STA_MODE || opmode == NT_EFAIL) {
		return FALSE;
	}

	if (NULL == please)
		return FALSE;

	if (dhcps_lease.enable == FALSE){
		if (nt_dhcps_status() == DHCP_STOPPED)
			return FALSE;
	} else {
	}

	ip_2_ip4(&please->start_ip)->addr = ip_2_ip4(&dhcps_lease.start_ip)->addr;
	ip_2_ip4(&please->end_ip)->addr = ip_2_ip4(&dhcps_lease.end_ip)->addr;
	return TRUE;
}

/**
 * @ingroup nt_dhcps
 * kill oldest dhcp pool.
 */
static void nt_kill_oldest_dhcps_pool(void)
{
	list_node *pre = NULL, *p = NULL;
	list_node *minpre = NULL, *minp = NULL;
	struct dhcps_pool *pdhcps_pool = NULL, *pmin_pool = NULL;
	pre = plist;
	p = pre->pnext;
	minpre = pre;
	minp = p;
	while (p != NULL){
		pdhcps_pool = p->pnode;
		pmin_pool = minp->pnode;
		if (pdhcps_pool->lease_timer < pmin_pool->lease_timer){
			minp = p;
			minpre = pre;
		}
		pre = p;
		p = p->pnext;
	}
	minpre->pnext = minp->pnext;
	nt_osal_free_memory(minp->pnode);
	minp->pnode = NULL;
	nt_osal_free_memory(minp);
	minp = NULL;
}

/**
 * @ingroup nt_dhcps
 * dhcp coarse timer to check if the lease is finished and remove from the list.
 */
void nt_dhcps_coarse_tmr(void)
{
	uint8_t num_dhcps_pool = 0;
	list_node *pback_node = NULL;
	list_node *pnode = NULL;
	struct dhcps_pool *pdhcps_pool = NULL;
	pnode = plist;
	while (pnode != NULL) {
		pdhcps_pool = pnode->pnode;
		if ( pdhcps_pool->type == DHCPS_TYPE_DYNAMIC) {
			pdhcps_pool->lease_timer --;
		}
		if (pdhcps_pool->lease_timer == 0){
			pback_node = pnode;
			pnode = pback_node->pnext;
			nt_dhcps_node_remove_from_list(&plist,pback_node);
			nt_osal_free_memory(pback_node->pnode);
			pback_node->pnode = NULL;
			nt_osal_free_memory(pback_node);
			pback_node = NULL;
		} else {
			pnode = pnode ->pnext;
			num_dhcps_pool++;
		}
	}

	if (num_dhcps_pool >= MAX_STATION_NUM)
		nt_kill_oldest_dhcps_pool();
}

/**
 * @ingroup nt_dhcps
 * API to set offer option in dhcp msg.
 */
NT_BOOL nt_set_dhcps_offer_option(uint8_t level, void* optarg)
{
	NT_BOOL offer_flag = TRUE;
	uint8_t option = 0;
	if (optarg == NULL && nt_dhcps_status() == FALSE)
		return FALSE;

	if (level <= OFFER_START || level >= OFFER_END)
		return FALSE;

	switch (level){
	case OFFER_ROUTER:
		dhcp_config->offer = (*(uint8_t *)optarg) & 0x01;
		offer_flag = TRUE;
		break;
	default :
		offer_flag = FALSE;
		break;
	}
	return offer_flag;
}

/**
 * @ingroup nt_dhcps
 * API to set dhcp lease time.
 *
 * @param time in minute.
 */
NT_BOOL nt_set_dhcps_lease_time(uint32_t minute)
{
	uint8_t opmode = nt_get_opmode();

	if (opmode == STA_MODE || opmode == NT_EFAIL) {
		return FALSE;
	}

	if (nt_dhcps_status() == DHCP_STARTED) {
		return FALSE;
	}

	if(minute == 0) {
		return FALSE;
	}
	dhcp_config->dhcps_lease_time = minute;
	return TRUE;
}

/**
 * @ingroup nt_dhcps
 * API to reset dhcp lease time to system default.
 */
NT_BOOL nt_reset_dhcps_lease_time(void)
{
	uint8_t opmode = nt_get_opmode();

	if (opmode == STA_MODE || opmode == NT_EFAIL) {
		return FALSE;
	}

	if (nt_dhcps_status() == DHCP_STARTED) {
		return FALSE;
	}
	dhcp_config->dhcps_lease_time = DHCPS_LEASE_TIME_DEF;
	return TRUE;
}

/**
 * @ingroup nt_dhcps
 * API to get dhcp lease time.
 */
uint32_t nt_get_dhcps_lease_time(void) // minute
{
	return dhcp_config->dhcps_lease_time;
}

/**
 * @ingroup nt_dhcps
 * Force to leave client from list.
 */
void nt_dhcps_client_leave(uint8_t *bssid, ip_addr_t *ip, NT_BOOL force)
{
	struct dhcps_pool *pdhcps_pool = NULL;
	list_node *pback_node = NULL;

	if ((bssid == NULL) || (ip == NULL)) {
		return;
	}

	for (pback_node = plist; pback_node != NULL;pback_node = pback_node->pnext) {
		pdhcps_pool = pback_node->pnode;
		if (pdhcps_pool == NULL) {
			NT_LOG_PRINT(COMMON,ERR,"Cleanup failed");
			assert(0);
		}
		if (memcmp(pdhcps_pool->mac, bssid, sizeof(pdhcps_pool->mac)) == 0){
			if (memcmp(&(ip_2_ip4(&pdhcps_pool->ip)->addr), &(ip_2_ip4(ip)->addr), sizeof(ip_2_ip4(&pdhcps_pool->ip)->addr)) == 0) {
				if ((pdhcps_pool->type == DHCPS_TYPE_STATIC) || (force)) {
					if(pback_node != NULL) {
						nt_dhcps_node_remove_from_list(&plist,pback_node);
						nt_osal_free_memory(pback_node);
						pback_node = NULL;
					}

					if (pdhcps_pool != NULL) {
						nt_osal_free_memory(pdhcps_pool);
						pdhcps_pool = NULL;
					}
				} else {
					pdhcps_pool->state = DHCPS_STATE_OFFLINE;
				}

				ip_addr_t ip_zero;
				memset(&ip_zero, 0x0, sizeof(ip_zero));
				//wifi_softap_set_station_info(bssid, &ip_zero);
				break;
			}
		}
	}
}

/**
 * @ingroup nt_dhcps
 * update client ip in the link list with the mac address.
 *
 * @param MAC address of client.
 * @param offered ip address.
 */
uint32_t nt_dhcps_client_update(uint8_t *bssid, ip_addr_t *ip)
{
	struct dhcps_pool *pdhcps_pool = NULL;
	list_node *pback_node = NULL;
	list_node *pmac_node = NULL;
	list_node *pip_node = NULL;
	NT_BOOL flag = FALSE;
	uint32_t start_ip = ip_2_ip4(&dhcps_lease.start_ip)->addr;
	uint32_t end_ip = ip_2_ip4(&dhcps_lease.end_ip)->addr;
	dhcps_type_t type = DHCPS_TYPE_DYNAMIC;
	if (bssid == NULL) {
		return IPADDR_ANY;
	}

	if (ip) {
		if (IPADDR_BROADCAST == ip_2_ip4(ip)->addr) {
			return IPADDR_ANY;
		} else if (IPADDR_ANY == ip_2_ip4(ip)->addr) {
			ip = NULL;
		} else {
			type = DHCPS_TYPE_STATIC;
		}
	}

	dhcp_config->renew = FALSE;
	for (pback_node = plist; pback_node != NULL;pback_node = pback_node->pnext) {
		pdhcps_pool = pback_node->pnode;
		//NT_LOG_PRINT(COMMON,INFO,"mac:"MACSTR"bssid:"MACSTR"",MAC2STR(pdhcps_pool->mac),MAC2STR(bssid));
		if (memcmp(pdhcps_pool->mac, bssid, sizeof(pdhcps_pool->mac)) == 0){
			pmac_node = pback_node;
			if (ip == NULL) {
				flag = TRUE;
				break;
			}
		}
		if (ip != NULL) {
			if (memcmp(&(ip_2_ip4(&pdhcps_pool->ip)->addr), &(ip_2_ip4(ip)->addr), sizeof(ip_2_ip4(&pdhcps_pool->ip)->addr)) == 0) {
				pip_node = pback_node;
			}
		} else if (flag == FALSE){
			if (memcmp(&(ip_2_ip4(&pdhcps_pool->ip)->addr), &start_ip, sizeof(ip_2_ip4(&pdhcps_pool->ip)->addr)) != 0) {
				flag = TRUE;
			} else {
				start_ip = htonl((ntohl(start_ip) + 1));
			}
		}
	}

	if ((ip == NULL) && (flag == FALSE)) {
		if (plist == NULL) {
			if (start_ip <= end_ip) {
				flag = TRUE;
			} else {
				return IPADDR_ANY;
			}
		} else {
			if (start_ip > end_ip) {
				return IPADDR_ANY;
			}
			//start_ip = htonl((ntohl(start_ip) + 1));
			flag = TRUE;
		}
	}

	if (pmac_node != NULL) { // update new ip
		if (pip_node != NULL){
			pdhcps_pool = pip_node->pnode;

			if (pip_node != pmac_node) {
				if(pdhcps_pool->state != DHCPS_STATE_OFFLINE) { // ip is used
					return IPADDR_ANY;
				}

				// mac exists and ip exists in other node,delete mac
				nt_dhcps_node_remove_from_list(&plist,pmac_node);
				nt_osal_free_memory(pmac_node->pnode);
				pmac_node->pnode = NULL;
				nt_osal_free_memory(pmac_node);
				pmac_node = pip_node;
				memscpy(pdhcps_pool->mac, sizeof(pdhcps_pool->mac), bssid, sizeof(pdhcps_pool->mac));
			} else {
				dhcp_config->renew = TRUE;
				type = DHCPS_TYPE_DYNAMIC;
			}

			pdhcps_pool->lease_timer = DHCPS_LEASE_TIMER;
			pdhcps_pool->type = type;
			pdhcps_pool->state = DHCPS_STATE_ONLINE;

		} else {
			pdhcps_pool = pmac_node->pnode;
			if (ip != NULL) {
				ip_2_ip4(&pdhcps_pool->ip)->addr = ip_2_ip4(ip)->addr;
			} else if (flag == TRUE) {
				ip_2_ip4(&pdhcps_pool->ip)->addr = start_ip;
			} else {    // no ip to distribute
				return IPADDR_ANY;
			}

			nt_dhcps_node_remove_from_list(&plist,pmac_node);
			pdhcps_pool->lease_timer = DHCPS_LEASE_TIMER;
			pdhcps_pool->type = type;
			pdhcps_pool->state = DHCPS_STATE_ONLINE;
			nt_dhcps_node_insert_to_list(&plist,pmac_node);
		}
	} else { // new station
		if (pip_node != NULL) { // maybe ip has used
			pdhcps_pool = pip_node->pnode;
			if (pdhcps_pool->state != DHCPS_STATE_OFFLINE) {
				return IPADDR_ANY;
			}
			memscpy(pdhcps_pool->mac, sizeof(pdhcps_pool->mac), bssid, sizeof(pdhcps_pool->mac));
			pdhcps_pool->lease_timer = DHCPS_LEASE_TIMER;
			pdhcps_pool->type = type;
			pdhcps_pool->state = DHCPS_STATE_ONLINE;
		} else {
			pdhcps_pool = (struct dhcps_pool *)nt_osal_calloc(1,sizeof(struct dhcps_pool));
			if (pdhcps_pool == NULL) {
				NT_LOG_PRINT(COMMON,ERR,"Memory alloc failed.");
				return 0;
			}

			if (ip != NULL) {
				ip_2_ip4(&pdhcps_pool->ip)->addr = ip_2_ip4(ip)->addr;
			} else if (flag == TRUE) {
				ip_2_ip4(&pdhcps_pool->ip)->addr = start_ip;
			} else {    // no ip to distribute
				nt_osal_free_memory(pdhcps_pool);
				return IPADDR_ANY;
			}
			if (ip_2_ip4(&pdhcps_pool->ip)->addr > end_ip) {
				nt_osal_free_memory(pdhcps_pool);
				return IPADDR_ANY;
			}
			memscpy(pdhcps_pool->mac, sizeof(pdhcps_pool->mac), bssid, sizeof(pdhcps_pool->mac));
			pdhcps_pool->lease_timer = DHCPS_LEASE_TIMER;
			pdhcps_pool->type = type;
			pdhcps_pool->state = DHCPS_STATE_ONLINE;
			pback_node = (list_node *)nt_osal_calloc(1,sizeof(list_node ));
			if (pback_node == NULL) {
                NT_LOG_PRINT(COMMON,ERR,"Memory alloc failed.");
				nt_osal_free_memory(pdhcps_pool);
				return IPADDR_ANY;
			}
			pback_node->pnode = pdhcps_pool;
			pback_node->pnext = NULL;
			nt_dhcps_node_insert_to_list(&plist,pback_node);
		}
	}

	return ip_2_ip4(&pdhcps_pool->ip)->addr;
}

/**
 * @ingroup nt_dhcps
 * API to start DHCP server.
 *
 * @param pointer to network interface
 */
NT_BOOL nt_ap_dhcps_start(struct netif * netif)
{
	int opmode = nt_get_opmode();
	NT_BOOL ret = FALSE;
	if(opmode == STA_MODE || opmode == NT_EFAIL) return FALSE;

	if(netif != NULL && (!netif->dhcps_flag)) {
		struct ip_info ipinfo;
		ret = nt_dpm_get_ip_info(nt_get_netifidx_by_devmode(AP_DEVICE), &ipinfo);
		if(ret == TRUE){
			nt_dhcps_start(netif, &ipinfo);
		}else return FALSE;
	}
	netif->dhcps_flag = TRUE;
	return TRUE;
}

/**
 * @ingroup nt_dhcps
 * API to stop DHCP server.
 *
 * @param pointer to network interface
 */
NT_BOOL nt_ap_dhcps_stop(struct netif *netif)
{
	int opmode = nt_get_opmode();
	if(opmode == STA_MODE || opmode == NT_EFAIL) return FALSE;

	if(netif == NULL){
		NT_LOG_PRINT(COMMON,ERR,"nt_ap_dhcps_stop: netif == NULL");
		return FALSE;
	}

	if(netif != NULL && netif->dhcps_flag) nt_dhcps_stop(netif);
	netif->dhcps_flag = FALSE;
	return TRUE;
}

/**
 * @ingroup nt_dhcps
 * API to provide DHCP server status.
 *
 */
enum dhcp_status nt_dhcps_status(void)
{
	enum dhcp_status status = DHCP_STOPPED;
	struct netif * nif = NULL;
	struct netif * netif = NULL;

	NETIF_FOREACH(netif){
		/* is the netif up, does it have a link and a valid address? */
		if (netif_is_up(netif) && netif_is_link_up(netif) && !ip4_addr_isany_val(*netif_ip4_addr(netif))) {
			nif = netif;
		}
	}
	if(nif != NULL)
		status = nif->dhcps_flag;
	return status;
}

/**
 * @ingroup nt_dhcps
 * API to provide DHCP server status.
 *
 * @param pointer to network interface
 */
enum dhcp_status nt_dhcps_netif_status(struct netif * netif)
{
	enum dhcp_status status = DHCP_STOPPED;

	if(netif != NULL)
		status = netif->dhcps_flag;
	return status;
}

#endif /* LWIP_IPV4 && NT_FN_DHCPS_V4 */
