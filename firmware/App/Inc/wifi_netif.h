/* wifi_netif.h - lwIP network interface over the RW007 (station mode, DHCP). */
#ifndef WIFI_NETIF_H
#define WIFI_NETIF_H

#include <stdint.h>

#include "rw007.h"

/* Starts lwIP and the RW007 and registers the network interface.
 * Returns 0 on success (negative = RW007 init error code). */
int wifi_netif_start(void);

/* Joins the network and waits for a DHCP address.
 * Returns 0 when connected with an IP, -1 if the join failed, -2 on DHCP timeout. */
int wifi_netif_connect(const char *ssid, const char *password, uint32_t security, uint32_t timeout_ms);

void wifi_netif_disconnect(void);
int wifi_netif_is_up(void);   /* associated and has an IP address */

/* Current addresses in network byte order (0 if none). */
void wifi_netif_get_addr(uint32_t *ip, uint32_t *mask, uint32_t *gw, uint32_t *dns);
void wifi_netif_get_mac(uint8_t mac[6]);

/* Runs a scan and logs every network found (diagnostics). */
void wifi_netif_scan_and_log(void);

#endif
