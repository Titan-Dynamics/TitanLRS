#include "usbnet.h"

#include <Arduino.h>

#if defined(PLATFORM_STM32)

#include "lwip/init.h"
#include "lwip/etharp.h"
#include "lwip/netif.h"
#include "lwip/pbuf.h"
#include "lwip/timeouts.h"
#include "netif/ethernet.h"

#include "usbd_ncm.h"
#include "dhcp_server.h"

// The one thing lwIP needs from the platform in a NO_SYS build: a millisecond clock.
extern "C" u32_t sys_now(void)
{
    return millis();
}

static struct netif s_netif;
static bool s_initialised = false;
static bool s_linkUp = false;

static err_t linkOutput(struct netif *netif, struct pbuf *p)
{
    (void)netif;
    uint8_t *dst = NCM_TxReserve(p->tot_len);
    if (dst == nullptr)
    {
        // Link down or the host has stopped reading: drop it, as a busy MAC would.
        return ERR_MEM;
    }
    pbuf_copy_partial(p, dst, p->tot_len, 0);
    NCM_TxCommit();
    NCM_TxKick();
    return ERR_OK;
}

static err_t netifInit(struct netif *netif)
{
    netif->name[0] = 'u';
    netif->name[1] = 's';
    netif->output = etharp_output;
    netif->linkoutput = linkOutput;
    netif->mtu = 1500;
    netif->hwaddr_len = ETH_HWADDR_LEN;
    NCM_DeviceMac(netif->hwaddr);
    netif->flags = NETIF_FLAG_BROADCAST | NETIF_FLAG_ETHARP | NETIF_FLAG_ETHERNET;
    return ERR_OK;
}

static void deliverFrame(const uint8_t *frame, uint16_t len, void *ctx)
{
    (void)ctx;
    struct pbuf *p = pbuf_alloc(PBUF_RAW, len, PBUF_POOL);
    if (p == nullptr)
    {
        return;
    }
    pbuf_take(p, frame, len);
    if (s_netif.input(p, &s_netif) != ERR_OK)
    {
        pbuf_free(p);
    }
}

void USBNet_Init()
{
    if (s_initialised)
    {
        return;
    }
    lwip_init();

    ip4_addr_t addr, mask, gw;
    IP4_ADDR(&addr, USBNET_NET_A, USBNET_NET_B, USBNET_SUBNET, USBNET_DEVICE_HOST_PART);
    IP4_ADDR(&mask, 255, 255, 255, 0);
    ip4_addr_set_zero(&gw);
    netif_add(&s_netif, &addr, &mask, &gw, nullptr, netifInit, ethernet_input);
    netif_set_default(&s_netif);
    netif_set_up(&s_netif);

    ip4_addr_t host;
    IP4_ADDR(&host, USBNET_NET_A, USBNET_NET_B, USBNET_SUBNET, USBNET_HOST_HOST_PART);
    DhcpServer_Init(&addr, &mask, &host);

    s_initialised = true;
}

void USBNet_Poll()
{
    if (!s_initialised)
    {
        return;
    }

    const bool up = NCM_LinkUp();
    if (up != s_linkUp)
    {
        s_linkUp = up;
        if (up)
        {
            netif_set_link_up(&s_netif);
        }
        else
        {
            netif_set_link_down(&s_netif);
        }
    }

    NCM_Service();
    NCM_Receive(deliverFrame, nullptr);
    sys_check_timeouts();
    NCM_TxKick();
}

bool USBNet_LinkUp()
{
    return s_linkUp;
}

#endif
