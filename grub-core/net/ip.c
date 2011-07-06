/*
 *  GRUB  --  GRand Unified Bootloader
 *  Copyright (C) 2010,2011  Free Software Foundation, Inc.
 *
 *  GRUB is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, either version 3 of the License, or
 *  (at your option) any later version.
 *
 *  GRUB is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with GRUB.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <grub/net/ip.h>
#include <grub/misc.h>
#include <grub/net/arp.h>
#include <grub/net/udp.h>
#include <grub/net/ethernet.h>
#include <grub/net.h>
#include <grub/net/netbuff.h>
#include <grub/mm.h>

struct iphdr {
  grub_uint8_t verhdrlen;
  grub_uint8_t service;
  grub_uint16_t len;
  grub_uint16_t ident;
  grub_uint16_t frags;
  grub_uint8_t ttl;
  grub_uint8_t protocol;
  grub_uint16_t chksum;
  grub_uint32_t src;
  grub_uint32_t  dest;
} __attribute__ ((packed)) ;

struct ip6hdr
{
  grub_uint8_t version:4, priority:4;
  grub_uint8_t flow_lbl[3];
  grub_uint16_t payload_len;
  grub_uint8_t nexthdr;
  grub_uint8_t hop_limit;
  grub_uint8_t saddr[16];
  grub_uint8_t daddr[16];
} __attribute__ ((packed));

grub_uint16_t
grub_net_ip_chksum (void *ipv, int len)
{
  grub_uint16_t *ip = (grub_uint16_t *) ipv;
  grub_uint32_t sum = 0;

  len >>= 1;
  while (len--)
    {
      sum += grub_be_to_cpu16 (*(ip++));
      if (sum > 0xFFFF)
	sum -= 0xFFFF;
    }

  return grub_cpu_to_be16 ((~sum) & 0x0000FFFF);
}

grub_err_t
grub_net_send_ip_packet (struct grub_net_network_level_interface * inf,
			 const grub_net_network_level_address_t * target,
			 struct grub_net_buff * nb)
{
  struct iphdr *iph;
  static int id = 0x2400;
  grub_net_link_level_address_t ll_target_addr;
  grub_err_t err;

  grub_netbuff_push (nb, sizeof (*iph));
  iph = (struct iphdr *) nb->data;

  iph->verhdrlen = ((4 << 4) | 5);
  iph->service = 0;
  iph->len = grub_cpu_to_be16 (nb->tail - nb->data);
  iph->ident = grub_cpu_to_be16 (++id);
  iph->frags = 0;
  iph->ttl = 0xff;
  iph->protocol = 0x11;
  iph->src = inf->address.ipv4;
  iph->dest = target->ipv4;

  iph->chksum = 0;
  iph->chksum = grub_net_ip_chksum ((void *) nb->data, sizeof (*iph));

  /* Determine link layer target address via ARP.  */
  err = grub_net_arp_resolve (inf, target, &ll_target_addr);
  if (err)
    return err;
  return send_ethernet_packet (inf, nb, ll_target_addr,
			       GRUB_NET_ETHERTYPE_IP);
}

grub_err_t
grub_net_recv_ip_packets (struct grub_net_buff * nb,
			  const struct grub_net_card * card,
			  const grub_net_link_level_address_t * hwaddress)
{
  struct iphdr *iph = (struct iphdr *) nb->data;
  grub_err_t err;
  struct grub_net_network_level_interface *inf = NULL;

  err = grub_netbuff_pull (nb, sizeof (*iph));
  if (err)
    return err;

  /* DHCP needs special treatment since we don't know IP yet.  */
  {
    struct udphdr *udph;
    udph = (struct udphdr *) nb->data;
    if (iph->protocol == IP_UDP && grub_be_to_cpu16 (udph->dst) == 68)
      {
	FOR_NET_NETWORK_LEVEL_INTERFACES (inf)
	  if (inf->card == card
	      && inf->address.type == GRUB_NET_NETWORK_LEVEL_PROTOCOL_DHCP_RECV
	      && grub_net_hwaddr_cmp (&inf->hwaddress, hwaddress) == 0)
	    {
	      err = grub_netbuff_pull (nb, sizeof (*udph));
	      if (err)
		return err;
	      grub_net_process_dhcp (nb, inf->card);
	      grub_netbuff_free (nb);
	    }
	return GRUB_ERR_NONE;
      }
  }

  if (!inf)
    {
      FOR_NET_NETWORK_LEVEL_INTERFACES (inf)
      {
	if (inf->card == card
	    && inf->address.type == GRUB_NET_NETWORK_LEVEL_PROTOCOL_IPV4
	    && inf->address.ipv4 == iph->dest
	    && grub_net_hwaddr_cmp (&inf->hwaddress, hwaddress) == 0)
	  break;
      }
    }

  switch (iph->protocol)
    {
    case IP_UDP:
      return grub_net_recv_udp_packet (nb, inf);
    default:
      grub_netbuff_free (nb);
      break;
    }

  return GRUB_ERR_NONE;
}
