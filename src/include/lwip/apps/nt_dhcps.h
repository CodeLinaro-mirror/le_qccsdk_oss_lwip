/*
 * Copyright (c) 2024 Qualcomm Innovation Center, Inc. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#ifndef LWIP_APPS_NT_DHCPS_H_
#define LWIP_APPS_NT_DHCPS_H_

#include "lwipopts.h"
#include "lwip/prot/dhcp.h"
#include "nt_common.h"
#include "wifi_cmn.h"
#include "fwconfig_cmn.h"
#include "nt_flags.h"
#include "ip_addr.h"
/*
 *                        Timeline diagram of messages exchanged between DHCP
 *                     client and servers when allocating a new network address
 *                     				Server          Client          Server
 *                               (not selected)                    (selected)
 *                                     v               v               v
 *                                     |               |               |
 *                                     |     Begins initialization     |
 *                                     |               |               |
 *                                     | _____________/|\____________  |
 *                                     |/DHCPDISCOVER | DHCPDISCOVER  \|
 *                                     |               |               |
 *                                 Determines          |          Determines
 *                                configuration        |         configuration
 *                                     |               |               |
 *                                     |\             |  ____________/ |
 *                                     | \________    | /DHCPOFFER     |
 *                                     | DHCPOFFER\   |/               |
 *                                     |           \  |                |
 *                                     |       Collects replies        |
 *                                     |             \|                |
 *                                     |     Selects configuration     |
 *                                     |               |               |
 *                                     | _____________/|\____________  |
 *                                     |/ DHCPREQUEST  |  DHCPREQUEST\ |
 *                                     |               |               |
 *                                     |               |     Commits configuration
 *                                     |               |               |
 *                                     |               | _____________/|
 *                                     |               |/ DHCPACK      |
 *                                     |               |               |
 *                                     |    Initialization complete    |
 *                                     |               |               |
 *                                     .               .               .
 *                                     .               .               .
 *                                     |               |               |
 *                                     |      Graceful shutdown        |
 *                                     |               |               |
 *                                     |               |\ ____________ |
 *                                     |               | DHCPRELEASE  \|
 *                                     |               |               |
 *                                     |               |        Discards lease
 *                                     |               |               |
 *                                     v               v               v
*/



#if LWIP_IPV4 && NT_FN_DHCPS_V4

/* DHCP message item offsets and length */
#define NT_DHCP_CHADDR_LEN   	16U
#define NT_DHCP_SNAME_LEN    	64U
#define NT_DHCP_FILE_LEN     	128U
#define NT_DHCPS_OPTIONS_LEN	312		/* Optional parameters field. 'options' field of at least length 312 octets defined in RFC 2131. */
  /** DHCP server */
#define  NT_PORT_DHCP_SERVER	67
  /** DHCP client */
#define  NT_PORT_DHCP_CLIENT	68

typedef struct dhcps_msg {
        uint8_t op;							/* packet opcode type. */
        uint8_t htype;						/* hardware addr type. */
        uint8_t hlen;						/* hardware addr length. */
        uint8_t hops;						/* gateway hops. */
        uint32_t xid;						/* transaction ID. */
        uint16_t secs;						/* seconds since boot began. */
        uint16_t flags;
        uint8_t ciaddr[NT_IPV4_ADDR_SIZE];	/* client IP address. */
        uint8_t yiaddr[NT_IPV4_ADDR_SIZE];	/* 'your' IP address. */
        uint8_t siaddr[NT_IPV4_ADDR_SIZE];	/* server IP address. */
        uint8_t giaddr[NT_IPV4_ADDR_SIZE];	/* gateway IP address. */
        uint8_t chaddr[NT_DHCP_CHADDR_LEN];	/* client hardware address. */
        uint8_t sname[NT_DHCP_SNAME_LEN];	/* Optional server host name, null terminated string. */
        uint8_t file[NT_DHCP_FILE_LEN];		/* Boot file name, null terminated string; "generic" name or null in DHCPDISCOVER, fully qualified directory-path name in DHCPOFFER. */
        uint8_t options[NT_DHCPS_OPTIONS_LEN];	/* Optional parameters field. 'options' field of at least length 312 octets defined in RFC 2131. */
}dhcps_msg;

typedef struct dhcps_state{
        int16_t state;						/* DHCP state info. */
} dhcps_state;

struct dhcps_lease {
	NT_BOOL enable;
	ip_addr_t start_ip;
	ip_addr_t end_ip;
	uint32_t  lease_time;
};

enum dhcps_offer_option{
	OFFER_START = 0x00,
	OFFER_ROUTER = 0x01,
	OFFER_END
};

typedef enum {
    DHCPS_TYPE_DYNAMIC,
    DHCPS_TYPE_STATIC
} dhcps_type_t;

typedef enum {
    DHCPS_STATE_ONLINE,
    DHCPS_STATE_OFFLINE
} dhcps_state_t;

struct dhcps_pool{
	ip_addr_t ip;
	uint8_t mac[6];
	uint32_t lease_timer;
    dhcps_type_t type;
    dhcps_state_t state;
};

typedef struct _list_node{
	void *pnode;
	struct _list_node *pnext;
}list_node;

enum dhcp_status {
    DHCP_STOPPED,
    DHCP_STARTED
};

/* DHCP server states */
typedef enum {
  DHCPS_STATE_OFF        = 0,
  DHCPS_STATE_OFFER      = 1,
  DHCPS_STATE_DECLINE    = 2,
  DHCPS_STATE_ACK        = 3,
  DHCPS_STATE_NAK        = 4,
  DHCPS_STATE_IDLE       = 5,
  DHCPS_STATE_RELEASE    = 6
} dhcps_state_enum_t;

typedef struct dhcps_arg {
	struct netif * netif;
	struct udp_pcb *pcb_dhcps;
	ip_addr_t dhcps_addr;
	ip_addr_t dhcpc_addr;
	uint32_t dhcps_lease_time;
	uint8_t offer;
	NT_BOOL renew;
} dhcps_arg_t;

/* IP address of network interface */
struct ip_info {
	ip_addr_t ip;				/* IP address of netif(network interface) */
	ip_addr_t netmask;			/* netmask of netif */
	ip_addr_t gw;				/* gateway of netif */
};

typedef enum{
	TURN_OFF_DHCP = 0,
	TURN_ON_DHCP = 1,
}dhcp_enable_disable;

#ifdef LWIP_DNS
#define NT_FN_USE_DNS
#endif

extern dhcps_arg_t *dhcp_config;

#define DHCPS_LEASE_TIMER	(dhcp_config->dhcps_lease_time)
#define DHCPS_MAX_LEASE 	0xFD					/* Maximum number of IP address in DHCP pool */
#define BOOTP_BROADCAST 	0x8000

/* BootP options */
#define DHCP_OPTION_PERFORM_ROUTER_DISCOVERY 31

//#define NT_USE_CLASS_B_NET 1
#define DHCPS_DEBUG          1
#define MAX_STATION_NUM      8

#define   dhcps_router_enabled(offer)	((offer & OFFER_ROUTER) != 0)

void nt_dhcps_stop(struct netif *netif);
enum dhcp_status nt_dhcps_status(void);
enum dhcp_status nt_dhcps_netif_status(struct netif * netif);
void nt_dhcps_client_leave(uint8_t *bssid, ip_addr_t *ip,NT_BOOL force);
uint32_t nt_dhcps_client_update(uint8_t *bssid, ip_addr_t *ip);
NT_BOOL nt_ap_dhcps_stop(struct netif *netif);
NT_BOOL nt_ap_dhcps_start(struct netif *netif);
NT_BOOL nt_set_dhcps_lease(struct dhcps_lease *please);
NT_BOOL nt_set_dhcps_lease_time(uint32_t minute);

#ifdef NT_FN_USE_DNS
void nt_set_dhcp_dns_s(ip_addr_t ipaddr);
ip_addr_t * dhcp_dns_getserver();
#endif // NT_FN_USE_DNS

#endif /* LWIP_IPV4 && NT_FN_DHCPS_V4 */
#endif /* LWIP_APPS_NT_DHCPS_H_ */
