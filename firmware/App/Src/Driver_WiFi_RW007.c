/*
 * Driver_WiFi_RW007.c - CMSIS-Driver WiFi (Driver_WiFi0) for the RT-Spark's RW007.
 *
 * The lab PDF configures an ISM43362 module through the CMSIS-Driver WiFi API
 * (Driver_WiFi0.Initialize / PowerControl / Activate, then sockets). The RT-Spark
 * has an RW007 instead, so this file implements the same API on top of the
 * RW007 driver and lwIP. The application therefore uses the PDF's sample code
 * unchanged.
 *
 * Implemented: Initialize, Uninitialize, PowerControl(FULL), GetModuleInfo,
 * Activate/Deactivate (station), IsConnected, GetNetInfo, SocketCreate (TCP),
 * SocketConnect, SocketSend, SocketRecv, SocketSetOpt (RCVTIMEO, SNDTIMEO,
 * KEEPALIVE), SocketClose, SocketGetHostByName. Everything else returns
 * ARM_DRIVER_ERROR_UNSUPPORTED / ARM_SOCKET_ENOTSUP.
 */
#include "Driver_WiFi.h"

#include <string.h>

#include "lwip/errno.h"
#include "lwip/netdb.h"
#include "lwip/sockets.h"

#include "rw007.h"
#include "wifi_netif.h"

#define ARM_WIFI_DRV_VERSION ARM_DRIVER_VERSION_MAJOR_MINOR(1, 0)

static const ARM_DRIVER_VERSION driver_version = {ARM_WIFI_API_VERSION, ARM_WIFI_DRV_VERSION};
static const ARM_WIFI_CAPABILITIES driver_capabilities = {
    .station = 1,
    .ap = 0,
    .station_ap = 0,
    .wps_station = 0,
    .wps_ap = 0,
    .event_ap_connect = 0,
    .event_ap_disconnect = 0,
    .event_eth_rx_frame = 0,
    .bypass_mode = 0,
    .ip = 1,
    .ip6 = 0,
    .ping = 0,
};

static int initialized, powered;
static char net_ssid[33], net_pass[65];
static uint8_t net_security = ARM_WIFI_SECURITY_UNKNOWN;

/* ---------------------------------------------------------------- control ---- */
static ARM_DRIVER_VERSION GetVersion(void) { return driver_version; }
static ARM_WIFI_CAPABILITIES GetCapabilities(void) { return driver_capabilities; }

static int32_t Initialize(ARM_WIFI_SignalEvent_t cb_event)
{
  (void)cb_event;   /* no asynchronous events are generated in station-only mode */
  initialized = 1;
  return ARM_DRIVER_OK;
}

static int32_t Uninitialize(void)
{
  initialized = 0;
  return ARM_DRIVER_OK;
}

static int32_t PowerControl(ARM_POWER_STATE state)
{
  if (!initialized)
    return ARM_DRIVER_ERROR;
  if (state != ARM_POWER_FULL)
    return ARM_DRIVER_ERROR_UNSUPPORTED;
  if (powered)
    return ARM_DRIVER_OK;
  if (wifi_netif_start() != 0)       /* resets the RW007, starts lwIP */
    return ARM_DRIVER_ERROR;
  powered = 1;
  return ARM_DRIVER_OK;
}

static int32_t GetModuleInfo(char *module_info, uint32_t max_len)
{
  char version[24];
  if (!powered || module_info == NULL || max_len == 0)
    return ARM_DRIVER_ERROR;
  rw007_get_version(version, sizeof version);
  strncpy(module_info, "RW007 ", max_len - 1);
  module_info[max_len - 1] = '\0';
  strncat(module_info, version, max_len - strlen(module_info) - 1);
  return ARM_DRIVER_OK;
}

static int32_t SetOption(uint32_t interface, uint32_t option, const void *data, uint32_t len)
{
  (void)interface; (void)option; (void)data; (void)len;
  return ARM_DRIVER_ERROR_UNSUPPORTED;
}

static int32_t GetOption(uint32_t interface, uint32_t option, void *data, uint32_t *len)
{
  if (interface != 0 || data == NULL || len == NULL || *len < 4)
    return ARM_DRIVER_ERROR_PARAMETER;
  uint32_t ip, mask, gw, dns;
  wifi_netif_get_addr(&ip, &mask, &gw, &dns);
  switch (option) {
  case ARM_WIFI_MAC:
    if (*len < 6)
      return ARM_DRIVER_ERROR_PARAMETER;
    wifi_netif_get_mac(data);
    *len = 6;
    return ARM_DRIVER_OK;
  case ARM_WIFI_IP:             memcpy(data, &ip, 4);   break;
  case ARM_WIFI_IP_SUBNET_MASK: memcpy(data, &mask, 4); break;
  case ARM_WIFI_IP_GATEWAY:     memcpy(data, &gw, 4);   break;
  case ARM_WIFI_IP_DNS1:        memcpy(data, &dns, 4);  break;
  default:
    return ARM_DRIVER_ERROR_UNSUPPORTED;
  }
  *len = 4;
  return ARM_DRIVER_OK;
}

static int32_t Scan(ARM_WIFI_SCAN_INFO_t scan_info[], uint32_t max_num)
{
  (void)scan_info; (void)max_num;
  return ARM_DRIVER_ERROR_UNSUPPORTED;
}

static int32_t Activate(uint32_t interface, const ARM_WIFI_CONFIG_t *config)
{
  uint32_t security;
  if (!powered)
    return ARM_DRIVER_ERROR;
  if (interface != 0 || config == NULL || config->ssid == NULL)
    return ARM_DRIVER_ERROR_PARAMETER;
  if (config->wps_method != ARM_WIFI_WPS_METHOD_NONE)
    return ARM_DRIVER_ERROR_UNSUPPORTED;
  switch (config->security) {
  case ARM_WIFI_SECURITY_OPEN: security = RW007_SECURITY_OPEN; break;
  case ARM_WIFI_SECURITY_WPA:  security = RW007_SECURITY_WPA_AES_PSK; break;
  case ARM_WIFI_SECURITY_WPA2: security = RW007_SECURITY_WPA2_AES_PSK; break;
  default: return ARM_DRIVER_ERROR_UNSUPPORTED;
  }

  int r = wifi_netif_connect(config->ssid, config->pass ? config->pass : "", security, 20000);
  if (r == -2)
    return ARM_DRIVER_ERROR_TIMEOUT;   /* associated but no DHCP address */
  if (r != 0)
    return ARM_DRIVER_ERROR;
  strncpy(net_ssid, config->ssid, sizeof net_ssid - 1);
  strncpy(net_pass, config->pass ? config->pass : "", sizeof net_pass - 1);
  net_security = config->security;
  return ARM_DRIVER_OK;
}

static int32_t Deactivate(uint32_t interface)
{
  if (interface != 0)
    return ARM_DRIVER_ERROR_PARAMETER;
  wifi_netif_disconnect();
  return ARM_DRIVER_OK;
}

static uint32_t IsConnected(void) { return (uint32_t)wifi_netif_is_up(); }

static int32_t GetNetInfo(ARM_WIFI_NET_INFO_t *net_info)
{
  if (net_info == NULL)
    return ARM_DRIVER_ERROR_PARAMETER;
  if (!wifi_netif_is_up())
    return ARM_DRIVER_ERROR;
  memset(net_info, 0, sizeof *net_info);
  strncpy(net_info->ssid, net_ssid, sizeof net_info->ssid - 1);
  strncpy(net_info->pass, net_pass, sizeof net_info->pass - 1);
  net_info->security = net_security;
  int rssi = rw007_get_rssi();
  net_info->rssi = (uint8_t)(rssi < 0 ? -rssi : rssi);
  return ARM_DRIVER_OK;
}

static int32_t BypassControl(uint32_t interface, uint32_t mode)
{
  (void)interface; (void)mode;
  return ARM_DRIVER_ERROR_UNSUPPORTED;
}
static int32_t EthSendFrame(uint32_t interface, const uint8_t *frame, uint32_t len)
{
  (void)interface; (void)frame; (void)len;
  return ARM_DRIVER_ERROR_UNSUPPORTED;
}
static int32_t EthReadFrame(uint32_t interface, uint8_t *frame, uint32_t len)
{
  (void)interface; (void)frame; (void)len;
  return ARM_DRIVER_ERROR_UNSUPPORTED;
}
static uint32_t EthGetRxFrameSize(uint32_t interface)
{
  (void)interface;
  return 0;
}

/* ---------------------------------------------------------------- sockets ---- */
static int32_t socket_error(void)
{
  switch (errno) {
  case EAGAIN:       return ARM_SOCKET_EAGAIN;
  case EINPROGRESS:  return ARM_SOCKET_EINPROGRESS;
  case ETIMEDOUT:    return ARM_SOCKET_ETIMEDOUT;
  case EISCONN:      return ARM_SOCKET_EISCONN;
  case ENOTCONN:     return ARM_SOCKET_ENOTCONN;
  case ECONNREFUSED: return ARM_SOCKET_ECONNREFUSED;
  case ECONNRESET:   return ARM_SOCKET_ECONNRESET;
  case ECONNABORTED: return ARM_SOCKET_ECONNABORTED;
  case EALREADY:     return ARM_SOCKET_EALREADY;
  case ENOMEM:       return ARM_SOCKET_ENOMEM;
  case EBADF:        return ARM_SOCKET_ESOCK;
  case EINVAL:       return ARM_SOCKET_EINVAL;
  default:           return ARM_SOCKET_ERROR;
  }
}

static int32_t SocketCreate(int32_t af, int32_t type, int32_t protocol)
{
  if (af != ARM_SOCKET_AF_INET || type != ARM_SOCKET_SOCK_STREAM ||
      (protocol != 0 && protocol != ARM_SOCKET_IPPROTO_TCP))
    return ARM_SOCKET_ENOTSUP;
  int s = lwip_socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
  return s < 0 ? ARM_SOCKET_ENOMEM : s;
}

static int32_t SocketBind(int32_t socket, const uint8_t *ip, uint32_t ip_len, uint16_t port)
{
  (void)socket; (void)ip; (void)ip_len; (void)port;
  return ARM_SOCKET_ENOTSUP;
}
static int32_t SocketListen(int32_t socket, int32_t backlog)
{
  (void)socket; (void)backlog;
  return ARM_SOCKET_ENOTSUP;
}
static int32_t SocketAccept(int32_t socket, uint8_t *ip, uint32_t *ip_len, uint16_t *port)
{
  (void)socket; (void)ip; (void)ip_len; (void)port;
  return ARM_SOCKET_ENOTSUP;
}

static int32_t SocketConnect(int32_t socket, const uint8_t *ip, uint32_t ip_len, uint16_t port)
{
  if (ip == NULL || ip_len != 4)
    return ARM_SOCKET_EINVAL;
  struct sockaddr_in addr;
  memset(&addr, 0, sizeof addr);
  addr.sin_len = sizeof addr;
  addr.sin_family = AF_INET;
  addr.sin_port = lwip_htons(port);
  memcpy(&addr.sin_addr.s_addr, ip, 4);
  if (lwip_connect(socket, (struct sockaddr *)&addr, sizeof addr) < 0)
    return socket_error();
  return 0;
}

static int32_t SocketRecv(int32_t socket, void *buf, uint32_t len)
{
  int n = lwip_recv(socket, buf, len, 0);
  if (n < 0)
    return socket_error();
  if (n == 0 && len > 0)
    return ARM_SOCKET_ECONNRESET;   /* peer closed the connection */
  return n;
}

static int32_t SocketRecvFrom(int32_t socket, void *buf, uint32_t len, uint8_t *ip, uint32_t *ip_len,
                              uint16_t *port)
{
  (void)ip; (void)ip_len; (void)port;
  return SocketRecv(socket, buf, len);
}

static int32_t SocketSend(int32_t socket, const void *buf, uint32_t len)
{
  int n = lwip_send(socket, buf, len, 0);
  return n < 0 ? socket_error() : n;
}

static int32_t SocketSendTo(int32_t socket, const void *buf, uint32_t len, const uint8_t *ip,
                            uint32_t ip_len, uint16_t port)
{
  (void)ip; (void)ip_len; (void)port;
  return SocketSend(socket, buf, len);
}

static int32_t SocketGetSockName(int32_t socket, uint8_t *ip, uint32_t *ip_len, uint16_t *port)
{
  (void)socket; (void)ip; (void)ip_len; (void)port;
  return ARM_SOCKET_ENOTSUP;
}
static int32_t SocketGetPeerName(int32_t socket, uint8_t *ip, uint32_t *ip_len, uint16_t *port)
{
  (void)socket; (void)ip; (void)ip_len; (void)port;
  return ARM_SOCKET_ENOTSUP;
}
static int32_t SocketGetOpt(int32_t socket, int32_t opt_id, void *opt_val, uint32_t *opt_len)
{
  (void)socket; (void)opt_id; (void)opt_val; (void)opt_len;
  return ARM_SOCKET_ENOTSUP;
}

static int32_t SocketSetOpt(int32_t socket, int32_t opt_id, const void *opt_val, uint32_t opt_len)
{
  if (opt_val == NULL || opt_len != sizeof(uint32_t))
    return ARM_SOCKET_EINVAL;
  uint32_t v;
  memcpy(&v, opt_val, sizeof v);
  int r;
  if (opt_id == ARM_SOCKET_SO_RCVTIMEO || opt_id == ARM_SOCKET_SO_SNDTIMEO) {
    struct timeval tv = {.tv_sec = (long)(v / 1000), .tv_usec = (long)((v % 1000) * 1000)};
    r = lwip_setsockopt(socket, SOL_SOCKET, opt_id == ARM_SOCKET_SO_RCVTIMEO ? SO_RCVTIMEO : SO_SNDTIMEO,
                        &tv, sizeof tv);
  } else if (opt_id == ARM_SOCKET_SO_KEEPALIVE) {
    int on = v != 0;
    r = lwip_setsockopt(socket, SOL_SOCKET, SO_KEEPALIVE, &on, sizeof on);
  } else {
    return ARM_SOCKET_ENOTSUP;
  }
  return r < 0 ? socket_error() : 0;
}

static int32_t SocketClose(int32_t socket)
{
  return lwip_close(socket) < 0 ? socket_error() : 0;
}

static int32_t SocketGetHostByName(const char *name, int32_t af, uint8_t *ip, uint32_t *ip_len)
{
  if (name == NULL || ip == NULL || ip_len == NULL || *ip_len < 4 || af != ARM_SOCKET_AF_INET)
    return ARM_SOCKET_EINVAL;
  struct hostent *h = lwip_gethostbyname(name);
  if (h == NULL || h->h_addr_list[0] == NULL)
    return ARM_SOCKET_EHOSTNOTFOUND;
  memcpy(ip, h->h_addr_list[0], 4);
  *ip_len = 4;
  return 0;
}

static int32_t Ping(const uint8_t *ip, uint32_t ip_len)
{
  (void)ip; (void)ip_len;
  return ARM_DRIVER_ERROR_UNSUPPORTED;
}

ARM_DRIVER_WIFI Driver_WiFi0 = {
    GetVersion,     GetCapabilities, Initialize,     Uninitialize,     PowerControl,   GetModuleInfo,
    SetOption,      GetOption,       Scan,           Activate,         Deactivate,     IsConnected,
    GetNetInfo,     BypassControl,   EthSendFrame,   EthReadFrame,     EthGetRxFrameSize,
    SocketCreate,   SocketBind,      SocketListen,   SocketAccept,     SocketConnect,  SocketRecv,
    SocketRecvFrom, SocketSend,      SocketSendTo,   SocketGetSockName, SocketGetPeerName,
    SocketGetOpt,   SocketSetOpt,    SocketClose,    SocketGetHostByName, Ping,
};
