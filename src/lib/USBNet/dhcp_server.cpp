#include "dhcp_server.h"

#if defined(PLATFORM_STM32)

#include <string.h>

#include "lwip/pbuf.h"
#include "lwip/udp.h"

// RFC 2131 / 2132
#define DHCP_SERVER_PORT 67
#define DHCP_CLIENT_PORT 68

#define BOOTP_REQUEST 1
#define BOOTP_REPLY 2

#define DHCP_DISCOVER 1
#define DHCP_OFFER 2
#define DHCP_REQUEST 3
#define DHCP_ACK 5

#define OPT_PAD 0
#define OPT_SUBNET_MASK 1
#define OPT_LEASE_TIME 51
#define OPT_MESSAGE_TYPE 53
#define OPT_SERVER_ID 54
#define OPT_END 255

#define BOOTP_FIXED_LEN 236  // op .. file, before the magic cookie
#define DHCP_MIN_LEN (BOOTP_FIXED_LEN + 4)
#define DHCP_REPLY_LEN 300   // fixed part + cookie + our options, padded to the BOOTP minimum
#define LEASE_SECONDS 86400UL

static const uint8_t magicCookie[4] = {99, 130, 83, 99};

static struct udp_pcb *s_pcb = nullptr;
static ip4_addr_t s_self;
static ip4_addr_t s_mask;
static ip4_addr_t s_host;

static int findMessageType(const uint8_t *msg, uint16_t len)
{
    uint16_t i = DHCP_MIN_LEN;
    while (i < len)
    {
        const uint8_t opt = msg[i];
        if (opt == OPT_END)
        {
            break;
        }
        if (opt == OPT_PAD)
        {
            i++;
            continue;
        }
        if (i + 1 >= len)
        {
            break;
        }
        const uint8_t optLen = msg[i + 1];
        if (opt == OPT_MESSAGE_TYPE && optLen == 1 && i + 2 < len)
        {
            return msg[i + 2];
        }
        i += 2 + optLen;
    }
    return -1;
}

static uint8_t *putOption(uint8_t *p, uint8_t code, const void *data, uint8_t len)
{
    *p++ = code;
    *p++ = len;
    memcpy(p, data, len);
    return p + len;
}

static void reply(const uint8_t *request, uint8_t messageType)
{
    struct pbuf *p = pbuf_alloc(PBUF_TRANSPORT, DHCP_REPLY_LEN, PBUF_RAM);
    if (p == nullptr)
    {
        return;
    }
    uint8_t *out = (uint8_t *)p->payload;
    memset(out, 0, DHCP_REPLY_LEN);

    out[0] = BOOTP_REPLY;
    out[1] = 1;                          // htype: Ethernet
    out[2] = 6;                          // hlen
    memcpy(&out[4], &request[4], 4);     // xid
    memcpy(&out[10], &request[10], 2);   // flags
    memcpy(&out[16], &s_host.addr, 4);   // yiaddr
    memcpy(&out[20], &s_self.addr, 4);   // siaddr
    memcpy(&out[28], &request[28], 16);  // chaddr
    memcpy(&out[BOOTP_FIXED_LEN], magicCookie, 4);

    uint8_t *opt = &out[DHCP_MIN_LEN];
    opt = putOption(opt, OPT_MESSAGE_TYPE, &messageType, 1);
    opt = putOption(opt, OPT_SERVER_ID, &s_self.addr, 4);
    const uint8_t lease[4] = {
        (uint8_t)(LEASE_SECONDS >> 24), (uint8_t)(LEASE_SECONDS >> 16),
        (uint8_t)(LEASE_SECONDS >> 8), (uint8_t)LEASE_SECONDS};
    opt = putOption(opt, OPT_LEASE_TIME, lease, 4);
    opt = putOption(opt, OPT_SUBNET_MASK, &s_mask.addr, 4);
    *opt = OPT_END;

    // The client has no address yet, so the reply goes to the link broadcast.
    udp_sendto(s_pcb, p, IP_ADDR_BROADCAST, DHCP_CLIENT_PORT);
    pbuf_free(p);
}

static void onReceive(void *arg, struct udp_pcb *pcb, struct pbuf *p, const ip_addr_t *addr, u16_t port)
{
    (void)arg;
    (void)pcb;
    (void)addr;
    (void)port;

    uint8_t msg[576];  // the largest DHCP message a client may send without negotiation
    const uint16_t len = pbuf_copy_partial(p, msg, sizeof(msg), 0);
    pbuf_free(p);

    if (len < DHCP_MIN_LEN || msg[0] != BOOTP_REQUEST || msg[1] != 1 || msg[2] != 6 ||
        memcmp(&msg[BOOTP_FIXED_LEN], magicCookie, 4) != 0)
    {
        return;
    }

    switch (findMessageType(msg, len))
    {
    case DHCP_DISCOVER:
        reply(msg, DHCP_OFFER);
        break;
    case DHCP_REQUEST:
        // There is only one address to give, so any request is for it.
        reply(msg, DHCP_ACK);
        break;
    default:
        break;
    }
}

void DhcpServer_Init(const ip4_addr_t *self, const ip4_addr_t *mask, const ip4_addr_t *host)
{
    if (s_pcb != nullptr)
    {
        return;
    }
    s_self = *self;
    s_mask = *mask;
    s_host = *host;

    s_pcb = udp_new();
    if (s_pcb == nullptr)
    {
        return;
    }
    ip_set_option(s_pcb, SOF_BROADCAST);
    udp_bind(s_pcb, IP_ADDR_ANY, DHCP_SERVER_PORT);
    udp_recv(s_pcb, onReceive, nullptr);
}

#endif
