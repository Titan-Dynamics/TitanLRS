#pragma once

#if defined(PLATFORM_STM32)

#include "lwip/ip4_addr.h"

/**
 * @brief Minimal DHCP server for the single host on the USB link.
 *
 * Every client gets `host`, with the subnet mask and a long lease — and deliberately no router
 * or DNS option, so the host never routes anything but this subnet through the adapter.
 */
void DhcpServer_Init(const ip4_addr_t *self, const ip4_addr_t *mask, const ip4_addr_t *host);

#endif
