/*
 * lwipopts.h - lwIP 2.1.2 configuration for the RT-Spark + RW007 Wi-Fi.
 *
 * Only what the firmware needs: IPv4, ARP, DHCP, DNS, TCP (MQTT) and the
 * BSD-style socket API, running in its own thread on FreeRTOS (CMSIS-RTOS v2).
 */
#ifndef LWIPOPTS_H
#define LWIPOPTS_H

#include "cmsis_os2.h"

#define NO_SYS                          0
#define SYS_LIGHTWEIGHT_PROT            1
#define LWIP_TCPIP_CORE_LOCKING         1

/* ---- memory ---- */
#define MEM_ALIGNMENT                   4
#define MEM_SIZE                        (10 * 1024)
#define MEMP_NUM_PBUF                   16
#define MEMP_NUM_UDP_PCB                4
#define MEMP_NUM_TCP_PCB                4
#define MEMP_NUM_TCP_PCB_LISTEN         1
#define MEMP_NUM_TCP_SEG                16
#define MEMP_NUM_NETBUF                 8
#define MEMP_NUM_NETCONN                6
#define PBUF_POOL_SIZE                  12
#define PBUF_POOL_BUFSIZE               1536

/* ---- protocols ---- */
#define LWIP_IPV4                       1
#define LWIP_IPV6                       0
#define LWIP_ARP                        1
#define LWIP_ETHERNET                   1
#define ETH_PAD_SIZE                    0
#define LWIP_ICMP                       1
#define LWIP_RAW                        0
#define LWIP_UDP                        1
#define LWIP_TCP                        1
#define LWIP_DHCP                       1
#define DHCP_DOES_ARP_CHECK             0
#define LWIP_DNS                        1
#define LWIP_IGMP                       0

#define TCP_MSS                         1460
#define TCP_SND_BUF                     (4 * TCP_MSS)
#define TCP_WND                         (2 * TCP_MSS)
#define TCP_SND_QUEUELEN                (4 * TCP_SND_BUF / TCP_MSS)
#define LWIP_TCP_KEEPALIVE              1

/* ---- network interface ---- */
#define LWIP_SINGLE_NETIF               1
#define LWIP_NETIF_LINK_CALLBACK        1
#define LWIP_NETIF_STATUS_CALLBACK      1
#define LWIP_NETIF_HOSTNAME             1
#define LWIP_NETIF_API                  1

/* ---- APIs ---- */
#define LWIP_NETCONN                    1
#define LWIP_SOCKET                     1
#define LWIP_COMPAT_SOCKETS             0   /* use lwip_xxx names; avoids clashing with newlib */
#define LWIP_POSIX_SOCKETS_IO_NAMES     0
#define LWIP_SO_RCVTIMEO                1
#define LWIP_SO_SNDTIMEO                1
#define LWIP_TIMEVAL_PRIVATE            0
#define LWIP_SOCKET_SELECT              1

/* ---- threads (stack sizes are in bytes for the CMSIS-RTOS v2 sys_arch) ---- */
#define TCPIP_THREAD_NAME               "tcpip"
#define TCPIP_THREAD_STACKSIZE          2048
#define TCPIP_THREAD_PRIO               osPriorityHigh
#define TCPIP_MBOX_SIZE                 8
#define DEFAULT_THREAD_STACKSIZE        1024
#define DEFAULT_THREAD_PRIO             osPriorityNormal
#define DEFAULT_TCP_RECVMBOX_SIZE       8
#define DEFAULT_UDP_RECVMBOX_SIZE       4
#define DEFAULT_RAW_RECVMBOX_SIZE       4
#define DEFAULT_ACCEPTMBOX_SIZE         4

/* ---- checksums in software (the RW007 hands over plain Ethernet frames) ---- */
#define CHECKSUM_GEN_IP                 1
#define CHECKSUM_GEN_UDP                1
#define CHECKSUM_GEN_TCP                1
#define CHECKSUM_GEN_ICMP               1
#define CHECKSUM_CHECK_IP               1
#define CHECKSUM_CHECK_UDP              1
#define CHECKSUM_CHECK_TCP              1
#define CHECKSUM_CHECK_ICMP             1

/* ---- debugging / statistics ---- */
#define LWIP_STATS                      0
#define LWIP_DEBUG                      0

#endif /* LWIPOPTS_H */
