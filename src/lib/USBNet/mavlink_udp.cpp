#include "mavlink_udp.h"

#if defined(PLATFORM_STM32)

#include <string.h>

#include "lwip/pbuf.h"
#include "lwip/udp.h"
#include "usbnet.h"

static struct udp_pcb *s_pcb = nullptr;
static MavlinkUdpReceiver s_receiver = nullptr;
static ip_addr_t s_remote;
static u16_t s_remotePort = MAVLINK_UDP_GCS_PORT;

static void onReceive(void *arg, struct udp_pcb *pcb, struct pbuf *p, const ip_addr_t *addr, u16_t port)
{
    (void)arg;
    (void)pcb;
    // Answer whoever is talking to us.
    ip_addr_copy(s_remote, *addr);
    s_remotePort = port;

    if (s_receiver != nullptr)
    {
        for (struct pbuf *q = p; q != nullptr; q = q->next)
        {
            s_receiver((const uint8_t *)q->payload, q->len);
        }
    }
    pbuf_free(p);
}

void MavlinkUdp_Begin(MavlinkUdpReceiver receiver)
{
    s_receiver = receiver;
    if (s_pcb != nullptr)
    {
        return;
    }
    USBNet_Init();
    IP_ADDR4(&s_remote, USBNET_NET_A, USBNET_NET_B, USBNET_SUBNET, USBNET_HOST_HOST_PART);

    s_pcb = udp_new();
    if (s_pcb == nullptr)
    {
        return;
    }
    udp_bind(s_pcb, IP_ADDR_ANY, MAVLINK_UDP_LOCAL_PORT);
    udp_recv(s_pcb, onReceive, nullptr);
}

void MavlinkUdp_Send(const uint8_t *data, uint16_t len)
{
    if (s_pcb == nullptr || len == 0 || !USBNet_LinkUp())
    {
        return;
    }
    struct pbuf *p = pbuf_alloc(PBUF_TRANSPORT, len, PBUF_RAM);
    if (p == nullptr)
    {
        return;
    }
    memcpy(p->payload, data, len);
    udp_sendto(s_pcb, p, &s_remote, s_remotePort);
    pbuf_free(p);
}

#endif
