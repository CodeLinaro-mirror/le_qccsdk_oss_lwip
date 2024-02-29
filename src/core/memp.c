/**
 * @file
 * Dynamic pool memory manager
 *
 * lwIP has dedicated pools for many structures (netconn, protocol control blocks,
 * packet buffers, ...). All these pools are managed here.
 *
 * @defgroup mempool Memory pools
 * @ingroup infrastructure
 * Custom memory pools

 */

/*
 * Copyright (c) 2001-2004 Swedish Institute of Computer Science.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without modification,
 * are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice,
 *    this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions and the following disclaimer in the documentation
 *    and/or other materials provided with the distribution.
 * 3. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR IMPLIED
 * WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT
 * SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT
 * OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING
 * IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY
 * OF SUCH DAMAGE.
 *
 * This file is part of the lwIP TCP/IP stack.
 *
 * Author: Adam Dunkels <adam@sics.se>
 *
 */

#include "lwip/opt.h"

#include "lwip/memp.h"
#include "lwip/sys.h"
#include "lwip/stats.h"

#include <string.h>

/* Make sure we include everything we need for size calculation required by memp_std.h */
#include "lwip/pbuf.h"
#include "lwip/raw.h"
#include "lwip/udp.h"
#include "lwip/tcp.h"
#include "lwip/priv/tcp_priv.h"
#include "lwip/altcp.h"
#include "lwip/ip4_frag.h"
#include "lwip/netbuf.h"
#include "lwip/api.h"
#include "lwip/priv/tcpip_priv.h"
#include "lwip/priv/api_msg.h"
#include "lwip/priv/sockets_priv.h"
#include "lwip/etharp.h"
#include "lwip/igmp.h"
#include "lwip/timeouts.h"
/* needed by default MEMP_NUM_SYS_TIMEOUT */
#include "netif/ppp/ppp_opts.h"
#include "lwip/netdb.h"
#include "lwip/dns.h"
#include "lwip/priv/nd6_priv.h"
#include "lwip/ip6_frag.h"
#include "lwip/mld6.h"

#define LWIP_MEMPOOL(name,num,size,desc) LWIP_MEMPOOL_DECLARE(name,num,size,desc)
#include "lwip/priv/memp_std.h"

const struct memp_desc *const memp_pools[MEMP_MAX] = {
#define LWIP_MEMPOOL(name,num,size,desc) &memp_ ## name,
#include "lwip/priv/memp_std.h"
};

#ifdef LWIP_HOOK_FILENAME
#include LWIP_HOOK_FILENAME
#endif

#if MEMP_MEM_MALLOC && MEMP_OVERFLOW_CHECK >= 2
#undef MEMP_OVERFLOW_CHECK
/* MEMP_OVERFLOW_CHECK >= 2 does not work with MEMP_MEM_MALLOC, use 1 instead */
#define MEMP_OVERFLOW_CHECK 1
#endif

#if MEMP_SANITY_CHECK && !MEMP_MEM_MALLOC
/**
 * Check that memp-lists don't form a circle, using "Floyd's cycle-finding algorithm".
 */
static int
memp_sanity(const struct memp_desc *desc)
{
  struct memp *t, *h;

  t = *desc->tab;
  if (t != NULL) {
    for (h = t->next; (t != NULL) && (h != NULL); t = t->next,
         h = ((h->next != NULL) ? h->next->next : NULL)) {
      if (t == h) {
        return 0;
      }
    }
  }

  return 1;
}
#endif /* MEMP_SANITY_CHECK && !MEMP_MEM_MALLOC */

#if MEMP_OVERFLOW_CHECK
/**
 * Check if a memp element was victim of an overflow or underflow
 * (e.g. the restricted area after/before it has been altered)
 *
 * @param p the memp element to check
 * @param desc the pool p comes from
 */
static void
memp_overflow_check_element(struct memp *p, const struct memp_desc *desc)
{
  mem_overflow_check_raw((u8_t *)p + MEMP_SIZE, desc->size, "pool ", desc->desc);
}

/**
 * Initialize the restricted area of on memp element.
 */
static void
memp_overflow_init_element(struct memp *p, const struct memp_desc *desc)
{
  mem_overflow_init_raw((u8_t *)p + MEMP_SIZE, desc->size);
}

#if MEMP_OVERFLOW_CHECK >= 2
/**
 * Do an overflow check for all elements in every pool.
 *
 * @see memp_overflow_check_element for a description of the check
 */
static void
memp_overflow_check_all(void)
{
  u16_t i, j;
  struct memp *p;
  SYS_ARCH_DECL_PROTECT(old_level);
  SYS_ARCH_PROTECT(old_level);

  for (i = 0; i < MEMP_MAX; ++i) {
    p = (struct memp *)LWIP_MEM_ALIGN(memp_pools[i]->base);
    for (j = 0; j < memp_pools[i]->num; ++j) {
      memp_overflow_check_element(p, memp_pools[i]);
      p = LWIP_ALIGNMENT_CAST(struct memp *, ((u8_t *)p + MEMP_SIZE + memp_pools[i]->size + MEM_SANITY_REGION_AFTER_ALIGNED));
    }
  }
  SYS_ARCH_UNPROTECT(old_level);
}
#endif /* MEMP_OVERFLOW_CHECK >= 2 */
#endif /* MEMP_OVERFLOW_CHECK */

/**
 * Initialize custom memory pool.
 * Related functions: memp_malloc_pool, memp_free_pool
 *
 * @param desc pool to initialize
 */
void
memp_init_pool(const struct memp_desc *desc)
{
#if MEMP_MEM_MALLOC
  LWIP_UNUSED_ARG(desc);
#else
  int i;
  struct memp *memp;

  *desc->tab = NULL;
  memp = (struct memp *)LWIP_MEM_ALIGN(desc->base);
#if MEMP_MEM_INIT
  /* force memset on pool memory */
  memset(memp, 0, (size_t)desc->num * (MEMP_SIZE + desc->size
#if MEMP_OVERFLOW_CHECK
                                       + MEM_SANITY_REGION_AFTER_ALIGNED
#endif
                                      ));
#endif
  /* create a linked list of memp elements */
  for (i = 0; i < desc->num; ++i) {
    memp->next = *desc->tab;
    *desc->tab = memp;
#if MEMP_OVERFLOW_CHECK
    memp_overflow_init_element(memp, desc);
#endif /* MEMP_OVERFLOW_CHECK */
    /* cast through void* to get rid of alignment warnings */
    memp = (struct memp *)(void *)((u8_t *)memp + MEMP_SIZE + desc->size
#if MEMP_OVERFLOW_CHECK
                                   + MEM_SANITY_REGION_AFTER_ALIGNED
#endif
                                  );
  }
#if MEMP_STATS
  desc->stats->avail = desc->num;
#endif /* MEMP_STATS */
#endif /* !MEMP_MEM_MALLOC */

#if MEMP_STATS && (defined(LWIP_DEBUG) || LWIP_STATS_DISPLAY)
  desc->stats->name  = desc->desc;
#endif /* MEMP_STATS && (defined(LWIP_DEBUG) || LWIP_STATS_DISPLAY) */
}

/**
 * Initializes lwIP built-in pools.
 * Related functions: memp_malloc, memp_free
 *
 * Carves out memp_memory into linked lists for each pool-type.
 */
void
memp_init(void)
{
  u16_t i;

#ifdef CONFIG_LWIP_HEAP_POOL
#if LWIP_MEM_PRE_ALLOC_FROM_HEAP
  /* Pre allocating memory for PCBs and pbufs from the heap */
#if LWIP_RAW
    memp_RAW_PCB.base=pvPortMalloc((MEMP_NUM_RAW_PCB) * (MEMP_SIZE + MEMP_ALIGN_SIZE(sizeof(struct raw_pcb))));
#endif
#if LWIP_UDP
    memp_UDP_PCB.base=pvPortMalloc((MEMP_NUM_UDP_PCB) * (MEMP_SIZE + MEMP_ALIGN_SIZE(sizeof(struct udp_pcb))));
#endif /* LWIP_UDP */
#if LWIP_TCP
    memp_TCP_PCB.base=pvPortMalloc((MEMP_NUM_TCP_PCB) * (MEMP_SIZE + MEMP_ALIGN_SIZE(sizeof(struct tcp_pcb))));
    memp_TCP_PCB_LISTEN.base=pvPortMalloc((MEMP_NUM_TCP_PCB_LISTEN) * (MEMP_SIZE + MEMP_ALIGN_SIZE(sizeof(struct tcp_pcb_listen))));
    memp_TCP_SEG.base=pvPortMalloc((MEMP_NUM_TCP_SEG) * (MEMP_SIZE + MEMP_ALIGN_SIZE(sizeof(struct tcp_seg))));
#endif /* LWIP_TCP */
#if LWIP_ALTCP && LWIP_TCP
    memp_ALTCP_PCB.base=pvPortMalloc((MEMP_NUM_ALTCP_PCB) * (MEMP_SIZE + MEMP_ALIGN_SIZE(sizeof(struct altcp_pcb))));
#endif /* LWIP_ALTCP && LWIP_TCP */
#if LWIP_IPV4 && IP_REASSEMBLY
    memp_REASSDATA.base=pvPortMalloc((MEMP_NUM_REASSDATA) * (MEMP_SIZE + MEMP_ALIGN_SIZE(sizeof(struct ip_reassdata))));
#endif /* LWIP_IPV4 && IP_REASSEMBLY */
#if (IP_FRAG && !LWIP_NETIF_TX_SINGLE_PBUF) || (LWIP_IPV6 && LWIP_IPV6_FRAG)
    memp_FRAG_PBUF.base=pvPortMalloc((MEMP_NUM_FRAG_PBUF) * (MEMP_SIZE + MEMP_ALIGN_SIZE(sizeof(struct pbuf_custom_ref))));
#endif /* LWIP_IPV4 && IP_REASSEMBLY */
#if LWIP_NETCONN || LWIP_SOCKET
    memp_NETBUF.base=pvPortMalloc((MEMP_NUM_NETBUF) * (MEMP_SIZE + MEMP_ALIGN_SIZE(sizeof(struct netbuf))));
    memp_NETCONN.base=pvPortMalloc((MEMP_NUM_NETCONN) * (MEMP_SIZE + MEMP_ALIGN_SIZE(sizeof(struct netconn))));
#endif /* LWIP_NETCONN || LWIP_SOCKET */
#if NO_SYS==0
    memp_TCPIP_MSG_API.base=pvPortMalloc((MEMP_NUM_TCPIP_MSG_API) * (MEMP_SIZE + MEMP_ALIGN_SIZE(sizeof(struct tcpip_msg))));
#if LWIP_MPU_COMPATIBLE
    memp_API_MSG.base=pvPortMalloc((MEMP_NUM_API_MSG) * (MEMP_SIZE + MEMP_ALIGN_SIZE(sizeof(struct api_msg))));
#if LWIP_DNS
    memp_DNS_API_MSG.base=pvPortMalloc((MEMP_NUM_DNS_API_MSG) * (MEMP_SIZE + MEMP_ALIGN_SIZE(sizeof(struct dns_api_msg))));
#endif
#if LWIP_SOCKET && !LWIP_TCPIP_CORE_LOCKING
    memp_SOCKET_SETGETSOCKOPT_DATA.base=pvPortMalloc((MEMP_NUM_SOCKET_SETGETSOCKOPT_DATA) * (MEMP_SIZE + MEMP_ALIGN_SIZE(sizeof(struct lwip_setgetsockopt_data))));
#endif
#if LWIP_SOCKET && (LWIP_SOCKET_SELECT || LWIP_SOCKET_POLL)
    memp_SELECT_CB.base=pvPortMalloc((MEMP_NUM_SELECT_CB) * (MEMP_SIZE + MEMP_ALIGN_SIZE(sizeof(struct lwip_select_cb))));
#endif /* LWIP_SOCKET && (LWIP_SOCKET_SELECT || LWIP_SOCKET_POLL) */
#if LWIP_NETIF_API
	memp_NETIFAPI_MSG.base=pvPortMalloc((MEMP_NUM_NETIFAPI_MSG) * (MEMP_SIZE + MEMP_ALIGN_SIZE(sizeof(struct netifapi_msg))));
#endif
#endif /* LWIP_MPU_COMPATIBLE */
#if !LWIP_TCPIP_CORE_LOCKING_INPUT
	memp_TCPIP_MSG_INPKT.base=pvPortMalloc((MEMP_NUM_TCPIP_MSG_INPKT) * (MEMP_SIZE + MEMP_ALIGN_SIZE(sizeof(struct tcpip_msg))));
#endif /* !LWIP_TCPIP_CORE_LOCKING_INPUT */
#endif /* NO_SYS==0 */
#if LWIP_IPV4 && LWIP_ARP && ARP_QUEUEING
	memp_ARP_QUEUE.base=pvPortMalloc((MEMP_NUM_ARP_QUEUE) * (MEMP_SIZE + MEMP_ALIGN_SIZE(sizeof(struct etharp_q_entry))));
#endif /* LWIP_IPV4 && LWIP_ARP && ARP_QUEUEING */
#if LWIP_IGMP
    memp_IGMP_GROUP.base=pvPortMalloc((MEMP_NUM_IGMP_GROUP) * (MEMP_SIZE + MEMP_ALIGN_SIZE(sizeof(struct igmp_group))));
#endif /* LWIP_IGMP */
#if LWIP_TIMERS && !LWIP_TIMERS_CUSTOM
    memp_SYS_TIMEOUT.base=pvPortMalloc((MEMP_NUM_SYS_TIMEOUT) * (MEMP_SIZE + MEMP_ALIGN_SIZE(sizeof(struct sys_timeo))));
#endif /* LWIP_TIMERS && !LWIP_TIMERS_CUSTOM */
#if LWIP_DNS && LWIP_SOCKET
    memp_NETDB.base=pvPortMalloc((MEMP_NUM_NETDB) * (MEMP_SIZE + MEMP_ALIGN_SIZE(NETDB_ELEM_SIZE)));
#endif /* LWIP_DNS && LWIP_SOCKET */
#if LWIP_DNS && DNS_LOCAL_HOSTLIST && DNS_LOCAL_HOSTLIST_IS_DYNAMIC
    memp_LOCALHOSTLIST.base=pvPortMalloc((MEMP_NUM_LOCALHOSTLIST) * (MEMP_SIZE + MEMP_ALIGN_SIZE(LOCALHOSTLIST_ELEM_SIZE)));
#endif /* LWIP_DNS && DNS_LOCAL_HOSTLIST && DNS_LOCAL_HOSTLIST_IS_DYNAMIC */
#if LWIP_IPV6 && LWIP_ND6_QUEUEING
    memp_ND6_QUEUE.base=pvPortMalloc((MEMP_NUM_ND6_QUEUE) * (MEMP_SIZE + MEMP_ALIGN_SIZE(sizeof(struct nd6_q_entry))));
#endif /* LWIP_IPV6 && LWIP_ND6_QUEUEING */
#if LWIP_IPV6 && LWIP_IPV6_REASS
    memp_IP6_REASSDATA.base=pvPortMalloc((MEMP_NUM_REASSDATA) * (MEMP_SIZE + MEMP_ALIGN_SIZE(sizeof(struct ip6_reassdata))));
#endif /* LWIP_IPV6 && LWIP_IPV6_REASS */

#if LWIP_IPV6 && LWIP_IPV6_MLD
    memp_MLD6_GROUP.base=pvPortMalloc((MEMP_NUM_MLD6_GROUP) * (MEMP_SIZE + MEMP_ALIGN_SIZE(sizeof(struct mld_group))));
#endif /* LWIP_IPV6 && LWIP_IPV6_MLD */
    memp_PBUF.base=pvPortMalloc((MEMP_NUM_PBUF) * (MEMP_SIZE + MEMP_ALIGN_SIZE(sizeof(struct pbuf))));
    memp_PBUF_POOL.base=pvPortMalloc((PBUF_POOL_SIZE) * (LWIP_MEM_ALIGN_SIZE(sizeof(struct pbuf)) + LWIP_MEM_ALIGN_SIZE(PBUF_POOL_BUFSIZE)));
#endif
#endif //CONFIG_LWIP_HEAP_POOL

  /* for every pool: */
  for (i = 0; i < LWIP_ARRAYSIZE(memp_pools); i++) {
    memp_init_pool(memp_pools[i]);

#if LWIP_STATS && MEMP_STATS
    lwip_stats.memp[i] = memp_pools[i]->stats;
#endif
  }

#if MEMP_OVERFLOW_CHECK >= 2
  /* check everything a first time to see if it worked */
  memp_overflow_check_all();
#endif /* MEMP_OVERFLOW_CHECK >= 2 */
}

static void *
#if !MEMP_OVERFLOW_CHECK
do_memp_malloc_pool(const struct memp_desc *desc)
#else
do_memp_malloc_pool_fn(const struct memp_desc *desc, const char *file, const int line)
#endif
{
  struct memp *memp;
  SYS_ARCH_DECL_PROTECT(old_level);

#if MEMP_MEM_MALLOC
  memp = (struct memp *)mem_malloc(MEMP_SIZE + MEMP_ALIGN_SIZE(desc->size));
  SYS_ARCH_PROTECT(old_level);
#else /* MEMP_MEM_MALLOC */
  SYS_ARCH_PROTECT(old_level);

  memp = *desc->tab;
#endif /* MEMP_MEM_MALLOC */

  if (memp != NULL) {
#if !MEMP_MEM_MALLOC
#if MEMP_OVERFLOW_CHECK == 1
    memp_overflow_check_element(memp, desc);
#endif /* MEMP_OVERFLOW_CHECK */

    *desc->tab = memp->next;
#if MEMP_OVERFLOW_CHECK
    memp->next = NULL;
#endif /* MEMP_OVERFLOW_CHECK */
#endif /* !MEMP_MEM_MALLOC */
#if MEMP_OVERFLOW_CHECK
    memp->file = file;
    memp->line = line;
#if MEMP_MEM_MALLOC
    memp_overflow_init_element(memp, desc);
#endif /* MEMP_MEM_MALLOC */
#endif /* MEMP_OVERFLOW_CHECK */
    LWIP_ASSERT("memp_malloc: memp properly aligned",
                ((mem_ptr_t)memp % MEM_ALIGNMENT) == 0);
#if MEMP_STATS
    desc->stats->used++;
    if (desc->stats->used > desc->stats->max) {
      desc->stats->max = desc->stats->used;
    }
#endif
    SYS_ARCH_UNPROTECT(old_level);
    /* cast through u8_t* to get rid of alignment warnings */
    return ((u8_t *)memp + MEMP_SIZE);
  } else {
#if MEMP_STATS
    desc->stats->err++;
#endif
    SYS_ARCH_UNPROTECT(old_level);
    LWIP_DEBUGF(MEMP_DEBUG | LWIP_DBG_LEVEL_SERIOUS, ("memp_malloc: out of memory in pool %s\n", desc->desc));
  }

  return NULL;
}

/**
 * Get an element from a custom pool.
 *
 * @param desc the pool to get an element from
 *
 * @return a pointer to the allocated memory or a NULL pointer on error
 */
void *
#if !MEMP_OVERFLOW_CHECK
memp_malloc_pool(const struct memp_desc *desc)
#else
memp_malloc_pool_fn(const struct memp_desc *desc, const char *file, const int line)
#endif
{
  LWIP_ASSERT("invalid pool desc", desc != NULL);
  if (desc == NULL) {
    return NULL;
  }

#if !MEMP_OVERFLOW_CHECK
  return do_memp_malloc_pool(desc);
#else
  return do_memp_malloc_pool_fn(desc, file, line);
#endif
}

/**
 * Get an element from a specific pool.
 *
 * @param type the pool to get an element from
 *
 * @return a pointer to the allocated memory or a NULL pointer on error
 */
void *
#if !MEMP_OVERFLOW_CHECK
memp_malloc(memp_t type)
#else
memp_malloc_fn(memp_t type, const char *file, const int line)
#endif
{
  void *memp;
  LWIP_ERROR("memp_malloc: type < MEMP_MAX", (type < MEMP_MAX), return NULL;);

#if MEMP_OVERFLOW_CHECK >= 2
  memp_overflow_check_all();
#endif /* MEMP_OVERFLOW_CHECK >= 2 */

#if !MEMP_OVERFLOW_CHECK
  memp = do_memp_malloc_pool(memp_pools[type]);
#else
  memp = do_memp_malloc_pool_fn(memp_pools[type], file, line);
#endif

  return memp;
}

static void
do_memp_free_pool(const struct memp_desc *desc, void *mem)
{
  struct memp *memp;
  SYS_ARCH_DECL_PROTECT(old_level);

  LWIP_ASSERT("memp_free: mem properly aligned",
              ((mem_ptr_t)mem % MEM_ALIGNMENT) == 0);

  /* cast through void* to get rid of alignment warnings */
  memp = (struct memp *)(void *)((u8_t *)mem - MEMP_SIZE);

  SYS_ARCH_PROTECT(old_level);

#if MEMP_OVERFLOW_CHECK == 1
  memp_overflow_check_element(memp, desc);
#endif /* MEMP_OVERFLOW_CHECK */

#if MEMP_STATS
  desc->stats->used--;
#endif

#if MEMP_MEM_MALLOC
  LWIP_UNUSED_ARG(desc);
  SYS_ARCH_UNPROTECT(old_level);
  mem_free(memp);
#else /* MEMP_MEM_MALLOC */
  memp->next = *desc->tab;
  *desc->tab = memp;

#if MEMP_SANITY_CHECK
  LWIP_ASSERT("memp sanity", memp_sanity(desc));
#endif /* MEMP_SANITY_CHECK */

  SYS_ARCH_UNPROTECT(old_level);
#endif /* !MEMP_MEM_MALLOC */
}

/**
 * Put a custom pool element back into its pool.
 *
 * @param desc the pool where to put mem
 * @param mem the memp element to free
 */
void
memp_free_pool(const struct memp_desc *desc, void *mem)
{
  LWIP_ASSERT("invalid pool desc", desc != NULL);
  if ((desc == NULL) || (mem == NULL)) {
    return;
  }

  do_memp_free_pool(desc, mem);
}

/**
 * Put an element back into its pool.
 *
 * @param type the pool where to put mem
 * @param mem the memp element to free
 */
void
memp_free(memp_t type, void *mem)
{
#ifdef LWIP_HOOK_MEMP_AVAILABLE
  struct memp *old_first;
#endif

  LWIP_ERROR("memp_free: type < MEMP_MAX", (type < MEMP_MAX), return;);

  if (mem == NULL) {
    return;
  }

#if MEMP_OVERFLOW_CHECK >= 2
  memp_overflow_check_all();
#endif /* MEMP_OVERFLOW_CHECK >= 2 */

#ifdef LWIP_HOOK_MEMP_AVAILABLE
  old_first = *memp_pools[type]->tab;
#endif

  do_memp_free_pool(memp_pools[type], mem);

#ifdef LWIP_HOOK_MEMP_AVAILABLE
  if (old_first == NULL) {
    LWIP_HOOK_MEMP_AVAILABLE(type);
  }
#endif
}
