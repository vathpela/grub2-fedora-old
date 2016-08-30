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

#include <grub/net/arp.h>
#include <grub/net/netbuff.h>
#include <grub/mm.h>
#include <grub/net.h>
#include <grub/net/ethernet.h>
#include <grub/net/ip.h>
#include <grub/time.h>

/* ARP header operation codes */
enum
  {
    ARP_REQUEST = 1,
    ARP_REPLY = 2
  };

enum
  {
    /* IANA defines ARP hardware types at
     * http://www.iana.org/assignments/arp-parameters/arp-parameters.xhtml
     */
    GRUB_NET_ARPHRD_ETHERNET = 1,
    GRUB_NET_ARPHRD_INFINIBAND = 32,
  };

struct arppkt_ether {
  grub_uint16_t hrd;
  grub_uint16_t pro;
  grub_uint8_t hln;
  grub_uint8_t pln;
  grub_uint16_t op;
  grub_uint8_t sender_mac[GRUB_NET_MAC_ADDR_SIZE_ETHERNET];
  grub_uint32_t sender_ip;
  grub_uint8_t recv_mac[GRUB_NET_MAC_ADDR_SIZE_ETHERNET];
  grub_uint32_t recv_ip;
} GRUB_PACKED;

struct arppkt_ipoib {
  grub_uint16_t hrd;
  grub_uint16_t pro;
  grub_uint8_t hln;
  grub_uint8_t pln;
  grub_uint16_t op;
  grub_uint8_t sender_mac[GRUB_NET_MAC_ADDR_SIZE_INFINIBAND];
  grub_uint32_t sender_ip;
  grub_uint8_t recv_mac[GRUB_NET_MAC_ADDR_SIZE_INFINIBAND];
  grub_uint32_t recv_ip;
} GRUB_PACKED;

union arppkt {
	struct {
		grub_uint16_t hrd;
		grub_uint16_t pro;
		grub_uint8_t hln;
		grub_uint8_t pln;
		grub_uint16_t op;
	} GRUB_PACKED;
	struct arppkt_ether ether;
	struct arppkt_ipoib ipoib;
};

static int have_pending;
static grub_uint32_t pending_req;

static inline grub_ssize_t
arp_pkt_size (grub_uint16_t hrd)
{
  switch (hrd) {
  case GRUB_NET_ARPHRD_ETHERNET:
    return sizeof (struct arppkt_ether);
  case GRUB_NET_ARPHRD_INFINIBAND:
    return sizeof (struct arppkt_ipoib);
  default:
    return -1;
  }
}

static inline grub_ssize_t
arp_hrd_from_card (struct grub_net_card *card)
{
  grub_link_level_protocol_id_t type = card->default_address.type;

  switch (type)
    {
    case GRUB_NET_LINK_LEVEL_PROTOCOL_ETHERNET:
      return GRUB_NET_ARPHRD_ETHERNET;
    case GRUB_NET_LINK_LEVEL_PROTOCOL_INFINIBAND:
      return GRUB_NET_ARPHRD_INFINIBAND;
    default:
      return -1;
    }
}

static inline grub_ssize_t
arp_mac_size_from_hrd (grub_uint8_t hrd)
{
  switch (hln)
    {
    case GRUB_NET_ARPHRD_ETHERNET:
      return GRUB_NET_MAC_ADDR_SIZE_ETHERNET;
    case GRUB_NET_ARPHRD_INFINIBAND:
      return GRUB_NET_MAC_ADDR_SIZE_INFINIBAND;
    default:
      return -1;
    }
}

grub_err_t
grub_net_arp_send_request (struct grub_net_network_level_interface *inf,
			   const grub_net_network_level_address_t *proto_addr)
{
  struct grub_net_buff nb;
  union arppkt *arp_packet;
  grub_net_link_level_address_t target_mac_addr;
  grub_err_t err;
  int i;
  grub_uint8_t *nbd;
  grub_uint8_t arp_data[128];
  grub_network_level_protocol_id_t type = inf->card.default_addr.type;

  if (proto_addr->type != GRUB_NET_NETWORK_LEVEL_PROTOCOL_IPV4)
    return grub_error (GRUB_ERR_BUG, "unsupported address family");

  /* Build a request packet.  */
  nb.head = arp_data;
  nb.end = arp_data + sizeof (arp_data);
  grub_netbuff_clear (&nb);
  grub_netbuff_reserve (&nb, 128);

  err = grub_netbuff_push (&nb, sizeof (*arp_packet));
  if (err)
    return err;

  arp_packet = (union arppkt *) nb.data;
  switch (type) {
    case GRUB_NET_LINK_LEVEL_PROTOCOL_ETHERNET:
	{
	  struct arppkt_ether *pkt = arp_packet.ether;

	  arp_packet->hrd =
	    grub_cpu_to_be16_compile_time (GRUB_NET_ARPHRD_ETHERNET);
	  arp_packet->hln = GRUB_NET_MAC_ADDR_SIZE_ETHERNET;
	  /* Sender hardware address.  */
	  grub_memcpy (pkt->sender_mac, &inf->hwaddress.mac,
		       GRUB_NET_MAC_ADDR_SIZE_ETHERNET);
	  pkt->sender_ip = inf->address.ipv4;
	  grub_memset (pkt->recv_mac, 0, GRUB_NET_MAC_ADDR_SIZE_ETHERNET);
	  pkt->recv_ip = proto_addr->ipv4;
	}
      break;
    case GRUB_NET_LINK_LEVEL_PROTOCOL_INFINIBAND:
	{
	  struct arppkt_ether *pkt = arp_packet.ipoib;

	  arp_packet->hrd =
	    grub_cpu_to_be16_compile_time (GRUB_NET_ARPHRD_INFINIBAND);
	  arp_packet->hln = GRUB_NET_MAC_ADDR_SIZE_INFINIBAND;
	  /* Sender hardware address.  */
	  grub_memcpy (pkt->sender_mac, &inf->hwaddress.mac,
		       GRUB_NET_MAC_ADDR_SIZE_INFINIBAND);
	  pkt->sender_ip = inf->address.ipv4;
	  grub_memset (pkt->recv_mac, 0, GRUB_NET_MAC_ADDR_SIZE_INFINIBAND);
	  pkt->recv_ip = proto_addr->ipv4;
	}
      break;
  }
  arp_packet->pro = grub_cpu_to_be16_compile_time (GRUB_NET_ETHERTYPE_IP);
  arp_packet->pln = 4;
  arp_packet->op = grub_cpu_to_be16_compile_time (ARP_REQUEST);
  /* Target protocol address */
  grub_memset (&target_mac_addr.mac, 0xff, mac_size);

  nbd = nb.data;
  send_ethernet_packet (inf, &nb, target_mac_addr, GRUB_NET_ETHERTYPE_ARP);
  for (i = 0; i < GRUB_NET_TRIES; i++)
    {
      if (grub_net_link_layer_resolve_check (inf, proto_addr))
	return GRUB_ERR_NONE;
      pending_req = proto_addr->ipv4;
      have_pending = 0;
      grub_net_poll_cards (GRUB_NET_INTERVAL + (i * GRUB_NET_INTERVAL_ADDITION),
                           &have_pending);
      if (grub_net_link_layer_resolve_check (inf, proto_addr))
	return GRUB_ERR_NONE;
      nb.data = nbd;
      send_ethernet_packet (inf, &nb, target_mac_addr, GRUB_NET_ETHERTYPE_ARP);
    }

  return GRUB_ERR_NONE;
}

grub_err_t
grub_net_arp_receive (struct grub_net_buff *nb,
		      struct grub_net_card *card)
{
  union arppkt *arp_packet = (union arppkt *) nb->data;
  grub_net_network_level_address_t sender_addr, target_addr;
  grub_net_link_level_address_t sender_mac_addr;
  struct grub_net_network_level_interface *inf;
  ssize_t hln = arp_mac_size_from_hrd (arp_packet->hrd);

  if (arp_packet->pro != grub_cpu_to_be16_compile_time (GRUB_NET_ETHERTYPE_IP)
      || arp_packet->pln != 4 || hln < 0 || arp_packet->hln != hln
      || nb->tail - nb->data < (int) arp_pkt_size (arp_packet->hrd))
    return GRUB_ERR_NONE;

  sender_addr.type = GRUB_NET_NETWORK_LEVEL_PROTOCOL_IPV4;
  target_addr.type = GRUB_NET_NETWORK_LEVEL_PROTOCOL_IPV4;

  switch (arp_packet->hrd)
    {
    case GRUB_NET_ARPHRD_ETHERNET:
	{
	  struct arppkt_ether *pkt = arp_packet.ether;

	  sender_addr.ipv4 = pkt->sender_ip;
	  target_addr.ipv4 = pkt->recv_ip;

	  if (pkt->sender_ip == pending_req)
	    have_pending = 1;

	  sender_mac_addr.type = GRUB_NET_LINK_LEVEL_PROTOCOL_ETHERNET;
	  grub_memcpy (sender_mac_addr.mac, pkt->sender_mac, hln);
	}
      break;
    case GRUB_NET_ARPHRD_INFINIBAND:
	{
	  struct arppkt_ipoib *pkt = arp_packet.ipoib;

	  sender_addr.ipv4 = pkt->sender_ip;
	  target_addr.ipv4 = pkt->recv_ip;

	  if (pkt->sender_ip == pending_req)
	    have_pending = 1;

	  sender_mac_addr.type = GRUB_NET_LINK_LEVEL_PROTOCOL_INFINIBAND;
	  grub_memcpy (sender_mac_addr.mac, pkt->sender_mac, hln);
	}
      break;
    default:
      return GRUB_ERR_NONE;
    }

  grub_net_link_layer_add_address (card, &sender_addr, &sender_mac_addr, 1);

  FOR_NET_NETWORK_LEVEL_INTERFACES (inf)
  {
    /* Am I the protocol address target? */
    if (grub_net_addr_cmp (&inf->address, &target_addr) == 0
	&& arp_packet->op == grub_cpu_to_be16_compile_time (ARP_REQUEST))
      {
        if ((nb->tail - nb->data) > 128)
          {
            grub_dprintf ("net", "arp packet with abnormal size (%ld bytes).\n",
                         nb->tail - nb->data);
            nb->tail = nb->data + 128;
          }
	grub_net_link_level_address_t target;
	struct grub_net_buff nb_reply;
	union arppkt *arp_reply;
	grub_uint8_t arp_data[128];
	grub_err_t err;

	nb_reply.head = arp_data;
	nb_reply.end = arp_data + sizeof (arp_data);
	grub_netbuff_clear (&nb_reply);
	grub_netbuff_reserve (&nb_reply, 128);

	err = grub_netbuff_push (&nb_reply, sizeof (*arp_packet));
	if (err)
	  return err;

	arp_reply = (unionarppkt *) nb_reply.data;

	arp_reply->hrd = arp_packet->hrd;
	arp_reply->pro = grub_cpu_to_be16_compile_time (GRUB_NET_ETHERTYPE_IP);
	arp_reply->hln = hln;
	arp_reply->pln = 4;
	arp_reply->op = grub_cpu_to_be16_compile_time (ARP_REPLY);

	switch (arp_packet->hrd)
	  {
	  case GRUB_NET_ARPHRD_ETHERNET:
	      {
		struct arppkt_ether *reply = arp_reply.ether;
		struct arppkt_ether *pkt = arp_packet.ether;

		reply->sender_ip = pkt->recv_ip;
		reply->recv_ip = pkt->sender_ip;
		target.type = GRUB_NET_LINK_LEVEL_PROTOCOL_ETHERNET;
		grub_memcpy (target.mac, pkt->sender_mac, hln);
		grub_memcpy (reply->sender_mac, inf->hwaddress.mac, hln);
		grub_memcpy (reply->recv_mac, pkt->sender_mac, hln);
	      }
	    break;
	  case GRUB_NET_ARPHRD_INFINIBAND:
	      {
		struct arppkt_ipoib *reply = arp_reply.ipoib;
		struct arppkt_ipoib *pkt = arp_packet.ipoib;

		reply->sender_ip = pkt->recv_ip;
		reply->recv_ip = pkt->sender_ip;
		target.type = GRUB_NET_LINK_LEVEL_PROTOCOL_INFINIBAND;
		grub_memcpy (target.mac, pkt->sender_mac, hln);
		grub_memcpy (reply->sender_mac, inf->hwaddress.mac, hln);
		grub_memcpy (reply->recv_mac, pkt->sender_mac, hln);
	      }
	    break;
	  }

	/* Change operation to REPLY and send packet */
	send_ethernet_packet (inf, &nb_reply, target, GRUB_NET_ETHERTYPE_ARP);
      }
  }
  return GRUB_ERR_NONE;
}
