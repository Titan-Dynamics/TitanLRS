#pragma once

#if defined(PLATFORM_STM32)

#include <stdint.h>

/*
 * USB network interface: lwIP running over the CDC-NCM function of the composite USB device.
 *
 * The host sees a USB Ethernet adapter on a private /24 of its own. We are .1 and hand the host
 * .2 over DHCP, with no router and no DNS server, so the adapter never becomes the host's route
 * to anywhere else. TX and RX use different subnets so both can be plugged into one computer.
 *
 * Everything here runs in the main loop: USBNet_Poll() moves received frames into lwIP, runs its
 * timers and starts transmissions. The USB interrupt never calls into lwIP.
 */

#define USBNET_SUBNET_TX 1
#define USBNET_SUBNET_RX 2

#if defined(TARGET_RX)
#define USBNET_SUBNET USBNET_SUBNET_RX
#else
#define USBNET_SUBNET USBNET_SUBNET_TX
#endif

// 10.73.<subnet>.1 is the device, .2 the host.
#define USBNET_NET_A 10
#define USBNET_NET_B 73
#define USBNET_DEVICE_HOST_PART 1
#define USBNET_HOST_HOST_PART 2

/** Bring up lwIP and the interface. Idempotent. */
void USBNet_Init();

/** Service the interface. Call every main-loop iteration. */
void USBNet_Poll();

/** True while the host has the network link up. */
bool USBNet_LinkUp();

#endif
