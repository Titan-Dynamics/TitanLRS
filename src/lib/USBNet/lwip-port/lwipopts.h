#pragma once

/*
 * lwIP configuration for the STM32 USB network interface (lib/USBNet).
 *
 * lwIP itself comes from the registry (stm32duino/STM32duino LwIP, targets/common.ini), which
 * includes this file while it compiles; the platform glue (arch/cc.h) is that library's.
 *
 * One netif (CDC-NCM over USB), IPv4, a static address, a DHCP server of our own, a small HTTP
 * server and a UDP socket for MAVLink. Everything runs from the main loop (USBNet_Poll): nothing
 * calls into lwIP from an interrupt, so there is no OS and no locking.
 */

/* ---- system --------------------------------------------------------------------------- */
#define NO_SYS                          1
#define SYS_LIGHTWEIGHT_PROT            0
#define LWIP_TIMERS                     1
#define LWIP_NETCONN                    0
#define LWIP_SOCKET                     0
#define LWIP_SINGLE_NETIF               1

/* ---- memory --------------------------------------------------------------------------- */
#define MEM_ALIGNMENT                   4
/* Also backs the HTTP server's request bodies and responses (mem_malloc), which reach ~24 KB for
 * a TX models import/export. */
#define MEM_SIZE                        (96 * 1024)
#define MEMP_NUM_PBUF                   16
#define MEMP_NUM_UDP_PCB                4
#define MEMP_NUM_TCP_PCB                6
#define MEMP_NUM_TCP_PCB_LISTEN         2
#define MEMP_NUM_TCP_SEG                24
#define PBUF_POOL_SIZE                  12
#define PBUF_POOL_BUFSIZE               1536

/* The heap and pools live in AXI SRAM: DTCM holds .bss, the heap and the stack and is the
 * scarce one. The section is NOLOAD (variants/ldscript_gen.ld), and lwIP initialises every
 * byte of these itself (mem_init / memp_init). */
#define LWIP_DECLARE_MEMORY_ALIGNED(variable_name, size) \
    u8_t variable_name[LWIP_MEM_ALIGN_BUFFER(size)] __attribute__((aligned(4), section(".axisram")))

/* ---- protocols ------------------------------------------------------------------------ */
#define LWIP_IPV4                       1
#define LWIP_IPV6                       0
#define LWIP_ARP                        1
#define LWIP_ETHERNET                   1
#define LWIP_ICMP                       1
#define LWIP_RAW                        0
#define LWIP_UDP                        1
#define LWIP_TCP                        1
#define LWIP_DHCP                       0   /* we are the DHCP server, not a client */
/* A DHCP client has no address yet and sends from 0.0.0.0, which ip4_input() otherwise drops as
 * an invalid (broadcast) source. Naming the server port switches that check off for it. */
#define LWIP_IP_ACCEPT_UDP_PORT(port)   ((port) == PP_NTOHS(67))
#define LWIP_AUTOIP                     0
#define LWIP_ACD                        0
#define LWIP_DNS                        0
#define LWIP_IGMP                       0
#define IP_REASSEMBLY                   0
#define IP_FRAG                         0

/* ---- TCP ------------------------------------------------------------------------------ */
#define TCP_MSS                         1460
#define TCP_WND                         (4 * TCP_MSS)
#define TCP_SND_BUF                     (4 * TCP_MSS)
#define TCP_SND_QUEUELEN                ((4 * TCP_SND_BUF) / TCP_MSS)
#define LWIP_TCP_KEEPALIVE              0

/* ---- netif ---------------------------------------------------------------------------- */
#define LWIP_NETIF_STATUS_CALLBACK      0
#define LWIP_NETIF_LINK_CALLBACK        0
#define LWIP_NETIF_HOSTNAME             0
#define ETH_PAD_SIZE                    0

/* ---- diagnostics ---------------------------------------------------------------------- */
#define LWIP_STATS                      0
#define LWIP_DEBUG                      0
/* The library's arch/cc.h reports a failed assertion with printf(), which links all of newlib's
 * printf (~10 KB) for messages nothing reads. Argument checks (LWIP_ERROR) are unaffected. */
#define LWIP_NOASSERT                   1
