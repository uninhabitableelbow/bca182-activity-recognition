/*
 * wifi_netif.c - lwIP 2.1.2 network interface on top of the RW007 driver.
 *
 * Outgoing Ethernet frames go to rw007_send_frame(); incoming frames from
 * the RW007 are copied into pbufs and handed to the tcpip thread.
 */
#include "wifi_netif.h"

#include <string.h>

#include "FreeRTOS.h"
#include "event_groups.h"
#include "task.h"

#include "lwip/dhcp.h"
#include "lwip/dns.h"
#include "lwip/etharp.h"
#include "lwip/netifapi.h"
#include "lwip/tcpip.h"
#include "netif/ethernet.h"

#include "app_log.h"

#define EV_CONNECTED    (1u << 0)
#define EV_CONNECT_FAIL (1u << 1)
#define EV_DISCONNECTED (1u << 2)
#define EV_SCAN_DONE    (1u << 3)

static struct netif wifi_if;
static EventGroupHandle_t events;
static uint8_t mac_addr[6];
static volatile int associated;

/* lwIP needs a millisecond clock (the FreeRTOS tick is 1 kHz). */
u32_t sys_now(void) { return (u32_t)xTaskGetTickCount(); }

/* ------------------------------------------------------------ lwIP <-> RW007 ---- */
static err_t low_level_output(struct netif *netif, struct pbuf *p)
{
  static uint8_t frame[1520];
  (void)netif;
  if (p->tot_len > sizeof frame)
    return ERR_IF;
  pbuf_copy_partial(p, frame, p->tot_len, 0);
  return rw007_send_frame(frame, p->tot_len) == 0 ? ERR_OK : ERR_MEM;
}

/* Called by the RW007 process task for each received frame. */
static void on_frame(const uint8_t *frame, uint32_t len)
{
  struct pbuf *p = pbuf_alloc(PBUF_RAW, (u16_t)len, PBUF_POOL);
  if (p == NULL)
    return;                                  /* out of buffers: drop, TCP will retransmit */
  pbuf_take(p, frame, (u16_t)len);
  if (wifi_if.input(p, &wifi_if) != ERR_OK)
    pbuf_free(p);
}

static void on_event(int event, const void *data)
{
  switch (event) {
  case RW007_EVT_CONNECT:
    associated = 1;
    xEventGroupSetBits(events, EV_CONNECTED);
    break;
  case RW007_EVT_CONNECT_FAIL:
    associated = 0;
    xEventGroupSetBits(events, EV_CONNECT_FAIL);
    break;
  case RW007_EVT_DISCONNECT:
    associated = 0;
    xEventGroupSetBits(events, EV_DISCONNECTED);
    break;
  case RW007_EVT_SCAN_REPORT: {
    const rw007_scan_result_t *r = data;
    app_log("  scan: %-32s ch %2d  rssi %4d  security 0x%08lX", r->ssid, r->channel, r->rssi,
            (unsigned long)r->security);
    break;
  }
  case RW007_EVT_SCAN_DONE:
    xEventGroupSetBits(events, EV_SCAN_DONE);
    break;
  default:
    break;
  }
}

static err_t wifi_if_init(struct netif *netif)
{
  netif->name[0] = 'w';
  netif->name[1] = '0';
  netif->hostname = "rt-spark";
  netif->output = etharp_output;
  netif->linkoutput = low_level_output;
  netif->mtu = 1500;
  netif->hwaddr_len = ETH_HWADDR_LEN;
  memcpy(netif->hwaddr, mac_addr, ETH_HWADDR_LEN);
  netif->flags = NETIF_FLAG_BROADCAST | NETIF_FLAG_ETHARP | NETIF_FLAG_ETHERNET;
  return ERR_OK;
}

/* ------------------------------------------------------------------ public ---- */
int wifi_netif_start(void)
{
  events = xEventGroupCreate();
  tcpip_init(NULL, NULL);

  int r = rw007_init(on_frame, on_event);
  if (r != 0)
    return r;

  char text[32];
  rw007_get_version(text, sizeof text);
  app_log("RW007 firmware version: %s", text);
  rw007_get_sn(text, sizeof text);
  app_log("RW007 serial number:    %s", text);
  rw007_get_mac(mac_addr);
  app_log("RW007 MAC address:      %02X:%02X:%02X:%02X:%02X:%02X", mac_addr[0], mac_addr[1],
          mac_addr[2], mac_addr[3], mac_addr[4], mac_addr[5]);

  ip4_addr_t zero;
  ip4_addr_set_zero(&zero);
  if (netifapi_netif_add(&wifi_if, &zero, &zero, &zero, NULL, wifi_if_init, tcpip_input) != ERR_OK)
    return -10;
  netifapi_netif_set_default(&wifi_if);
  netifapi_netif_set_up(&wifi_if);
  return 0;
}

int wifi_netif_connect(const char *ssid, const char *password, uint32_t security, uint32_t timeout_ms)
{
  xEventGroupClearBits(events, EV_CONNECTED | EV_CONNECT_FAIL | EV_DISCONNECTED);
  int r = rw007_join(ssid, password, security);
  if (r != 0)
    app_log("RW007 join command returned %d", r);

  EventBits_t ev = xEventGroupWaitBits(events, EV_CONNECTED | EV_CONNECT_FAIL, pdTRUE, pdFALSE,
                                       pdMS_TO_TICKS(timeout_ms));
  if (!(ev & EV_CONNECTED))
    return -1;

  netifapi_netif_set_link_up(&wifi_if);
  netifapi_dhcp_start(&wifi_if);
  TickType_t start = xTaskGetTickCount();
  while (!dhcp_supplied_address(&wifi_if)) {
    if (xTaskGetTickCount() - start > pdMS_TO_TICKS(15000))
      return -2;
    vTaskDelay(pdMS_TO_TICKS(100));
  }
  return 0;
}

void wifi_netif_disconnect(void)
{
  netifapi_dhcp_stop(&wifi_if);
  netifapi_netif_set_link_down(&wifi_if);
  rw007_disconnect();
  associated = 0;
}

int wifi_netif_is_up(void)
{
  return associated && netif_is_link_up(&wifi_if) && dhcp_supplied_address(&wifi_if);
}

void wifi_netif_get_addr(uint32_t *ip, uint32_t *mask, uint32_t *gw, uint32_t *dns)
{
  if (ip)   *ip = ip4_addr_get_u32(netif_ip4_addr(&wifi_if));
  if (mask) *mask = ip4_addr_get_u32(netif_ip4_netmask(&wifi_if));
  if (gw)   *gw = ip4_addr_get_u32(netif_ip4_gw(&wifi_if));
  if (dns)  *dns = ip4_addr_get_u32(ip_2_ip4(dns_getserver(0)));
}

void wifi_netif_get_mac(uint8_t mac[6]) { memcpy(mac, mac_addr, 6); }

void wifi_netif_scan_and_log(void)
{
  app_log("Scanning for Wi-Fi networks...");
  xEventGroupClearBits(events, EV_SCAN_DONE);
  rw007_scan();
  xEventGroupWaitBits(events, EV_SCAN_DONE, pdTRUE, pdFALSE, pdMS_TO_TICKS(8000));
}
