/* Networking DHCPv4 client */

/*
 * Copyright (c) 2017 ARM Ltd.
 * Copyright (c) 2016 Intel Corporation.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(net_dhcpv4_client_sample, LOG_LEVEL_DBG);

#include <zephyr/kernel.h>
#include <zephyr/linker/sections.h>
#include <errno.h>
#include <stdio.h>

#include <zephyr/net/net_if.h>
#include <zephyr/net/net_core.h>
#include <zephyr/net/net_context.h>
#include <zephyr/net/net_mgmt.h>

#include <zephyr/net/net_stats.h>
#include <zephyr/net/ethernet.h>

#define DHCP_OPTION_NTP (42)

static uint8_t ntp_server[4];

static struct net_mgmt_event_callback mgmt_cb;

static struct net_dhcpv4_option_callback dhcp_cb;

K_SEM_DEFINE(my_sem, 0, 1);

void my_timer_handler(struct k_timer *dummy)
{
	k_sem_give(&my_sem);
}

K_TIMER_DEFINE(my_timer, my_timer_handler, NULL);

static void start_dhcpv4_client(struct net_if *iface, void *user_data)
{
	ARG_UNUSED(user_data);

	LOG_INF("Start on %s: index=%d", net_if_get_device(iface)->name,
		net_if_get_by_iface(iface));
	net_dhcpv4_start(iface);
}

static void handler(struct net_mgmt_event_callback *cb,
		    uint32_t mgmt_event,
		    struct net_if *iface)
{
	int i = 0;

	if (mgmt_event != NET_EVENT_IPV4_ADDR_ADD) {
		return;
	}

	for (i = 0; i < NET_IF_MAX_IPV4_ADDR; i++) {
		char buf[NET_IPV4_ADDR_LEN];

		if (iface->config.ip.ipv4->unicast[i].ipv4.addr_type !=
							NET_ADDR_DHCP) {
			continue;
		}

		LOG_INF("   Address[%d]: %s", net_if_get_by_iface(iface),
			net_addr_ntop(AF_INET,
			    &iface->config.ip.ipv4->unicast[i].ipv4.address.in_addr,
						  buf, sizeof(buf)));
		LOG_INF("    Subnet[%d]: %s", net_if_get_by_iface(iface),
			net_addr_ntop(AF_INET,
				       &iface->config.ip.ipv4->unicast[i].netmask,
				       buf, sizeof(buf)));
		LOG_INF("    Router[%d]: %s", net_if_get_by_iface(iface),
			net_addr_ntop(AF_INET,
						 &iface->config.ip.ipv4->gw,
						 buf, sizeof(buf)));
		LOG_INF("Lease time[%d]: %u seconds", net_if_get_by_iface(iface),
			iface->config.dhcpv4.lease_time);
	}
}

static void option_handler(struct net_dhcpv4_option_callback *cb,
			   size_t length,
			   enum net_dhcpv4_msg_type msg_type,
			   struct net_if *iface)
{
	char buf[NET_IPV4_ADDR_LEN];

	LOG_INF("DHCP Option %d: %s", cb->option,
		net_addr_ntop(AF_INET, cb->data, buf, sizeof(buf)));
}

int main(void)
{
	LOG_INF("Run dhcpv4 client");

	net_mgmt_init_event_callback(&mgmt_cb, handler,
				     NET_EVENT_IPV4_ADDR_ADD);
	net_mgmt_add_event_callback(&mgmt_cb);

	net_dhcpv4_init_option_callback(&dhcp_cb, option_handler,
					DHCP_OPTION_NTP, ntp_server,
					sizeof(ntp_server));

	net_dhcpv4_add_option_callback(&dhcp_cb);

	net_if_foreach(start_dhcpv4_client, NULL);
	
	k_timer_start(&my_timer, K_SECONDS(1), K_SECONDS(1));
	
	while (true)
	{
		k_sem_take(&my_sem, K_FOREVER);
		
		struct net_if * iface = net_if_get_first_by_type(&NET_L2_GET_NAME(ETHERNET));
		if (iface == NULL)
		{
			LOG_ERR("%s: Ethernet interface not found", __func__);
			return -1;
		}

		const struct ethernet_api * api = (const struct ethernet_api *)net_if_get_device(iface)->api;
		struct net_stats_eth * eth_stats = NULL;

		if (!api->get_stats)
		{
			LOG_INF("%s: get_stats API not found", __func__);
			return -1;
		}

		eth_stats = api->get_stats(net_if_get_device(iface));
		if (!eth_stats)
		{
			LOG_INF("%s: eth stats not found", __func__);
			return -1;
		}
		
		if (eth_stats->error_details.rx_dma_failed)
		{
			LOG_ERR("DMA ERRORS! %08X", eth_stats->error_details.rx_dma_failed);
			eth_stats->error_details.rx_dma_failed = 0;
		}
		
		if (eth_stats->error_details.mac_errors)
		{
			LOG_ERR("MAC ERRORS! %08X", eth_stats->error_details.mac_errors);
			eth_stats->error_details.mac_errors = 0;
		}
	}
	
	return 0;
}
