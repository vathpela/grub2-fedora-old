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

#include <grub/net/netbuff.h>
#include <grub/dl.h>
#include <grub/net.h>
#include <grub/net/ethernet.h>
#include <grub/time.h>
#include <grub/efi/api.h>
#include <grub/efi/efi.h>
#include <grub/i18n.h>

#include "efinet.h"

GRUB_MOD_LICENSE ("GPLv3+");

/* GUID.  */
static grub_efi_guid_t pxe_io_guid = GRUB_EFI_PXE_GUID;
static grub_efi_guid_t snp_io_guid = GRUB_EFI_SIMPLE_NETWORK_GUID;

static grub_efi_status_t
start (grub_efi_simple_network_t *net)
{
  return efi_call_1 (net->start, net);
}

static grub_efi_status_t
stop (grub_efi_simple_network_t *net)
{
  return efi_call_1 (net->stop, net);
}

static grub_efi_status_t
initialize (grub_efi_simple_network_t *net,
	    grub_efi_uintn_t extra_rx_buffer_size,
	    grub_efi_uintn_t extra_tx_buffer_size)
{
  return efi_call_3 (net->initialize, net, extra_rx_buffer_size,
		     extra_tx_buffer_size);
}

static grub_efi_status_t
shutdown (grub_efi_simple_network_t *net)
{
  return efi_call_1 (net->shutdown, net);
}

static grub_efi_status_t
rx_filters (grub_efi_simple_network_t *net,
	    grub_efi_uint32_t enable,
	    grub_efi_uint32_t disable,
	    grub_efi_boolean_t reset_mcast_filter,
	    grub_efi_uintn_t mcast_filter_count,
	    grub_efi_mac_address_t *mcast_filter)
{
  return efi_call_6 (net->receive_filters, net, enable, disable,
		     reset_mcast_filter, mcast_filter_count,
		     mcast_filter);
}

static grub_efi_status_t
tx (grub_efi_simple_network_t *net,
    grub_efi_uintn_t header_size,
    grub_efi_uintn_t buffer_size,
    void *buffer,
    grub_efi_mac_address_t *src,
    grub_efi_mac_address_t *dest,
    grub_efi_uint16_t *proto)
{
  return efi_call_7 (net->transmit, net, header_size, buffer_size, buffer,
		     src, dest, proto);
}

static grub_efi_status_t
rx (grub_efi_simple_network_t *net,
    grub_efi_uintn_t *header_size,
    grub_efi_uintn_t *buffer_size,
    void *buffer,
    grub_efi_mac_address_t *src,
    grub_efi_mac_address_t *dest,
    grub_efi_uint16_t *proto)
{
  return efi_call_7 (net->receive, net, header_size, buffer_size, buffer,
		     src, dest, proto);
}

static grub_efi_status_t
get_status (grub_efi_simple_network_t *net,
	    grub_efi_uint32_t *status,
	    void **txbuf)
{
  return efi_call_3 (net->get_status, net, status, txbuf);
}

static grub_err_t
send_card_buffer (struct grub_net_card *dev,
		  struct grub_net_buff *pack)
{
  grub_efi_status_t st;
  grub_efi_snp_data_t *priv = &dev->efi_net_info->snp_data;
  grub_efi_simple_network_t *net = priv->snp;
  grub_uint64_t limit_time = grub_get_time_ms () + 4000;
  void *txbuf;
  int retry = 0;

  if (dev->txbuf == NULL)
    {
      txbuf = NULL;
      st = get_status (net, 0, &txbuf);
      if (st != GRUB_EFI_SUCCESS || txbuf == NULL)
	txbuf = grub_zalloc (dev->mtu);
      if (txbuf == NULL)
	{
	  grub_print_error ();
	  return GRUB_ERR_OUT_OF_MEMORY;
	}
      dev->txbuf = txbuf;
    }

  if (dev->txbusy)
    while (1)
      {
	txbuf = NULL;
	st = get_status (net, 0, &txbuf);
	if (st != GRUB_EFI_SUCCESS)
	  return grub_error (GRUB_ERR_IO,
			     N_("couldn't send network packet"));
	/*
	   Some buggy firmware could return an arbitrary address instead of the
	   txbuf address we trasmitted, so just check that txbuf is non NULL
	   for success.  This is ok because we open the SNP protocol in
	   exclusive mode so we know we're the only ones transmitting on this
	   box and since we only transmit one packet at a time we know our
	   transmit was successfull.
	 */
	if (txbuf)
	  {
	    dev->txbusy = 0;
	    break;
	  }
	if (!retry)
	  {
	    st = tx (net, 0, priv->last_pkt_size, dev->txbuf,
			 NULL, NULL, NULL);
	    if (st != GRUB_EFI_SUCCESS)
	      return grub_error (GRUB_ERR_IO,
				 N_("couldn't send network packet"));
	    retry = 1;
	  }
	if (limit_time < grub_get_time_ms ())
	  return grub_error (GRUB_ERR_TIMEOUT,
			     N_("couldn't send network packet"));
      }

  priv->last_pkt_size = (pack->tail - pack->data);
  if (priv->last_pkt_size > dev->mtu)
    priv->last_pkt_size = dev->mtu;

  grub_memcpy (dev->txbuf, pack->data, priv->last_pkt_size);

  st = tx (net, 0, priv->last_pkt_size, dev->txbuf,
			 NULL, NULL, NULL);
  if (st != GRUB_EFI_SUCCESS)
    return grub_error (GRUB_ERR_IO, N_("couldn't send network packet"));

  /*
     The card may have sent out the packet immediately - set txbusy
     to 0 in this case.
     Cases were observed where checking txbuf at the next call
     of send_card_buffer() is too late: 0 is returned in txbuf and
     we run in the GRUB_ERR_TIMEOUT case above.
     Perhaps a timeout in the FW has discarded the recycle buffer.
   */
  txbuf = NULL;
  st = get_status (net, 0, &txbuf);
  dev->txbusy = !(st == GRUB_EFI_SUCCESS && txbuf);

  return GRUB_ERR_NONE;
}

static struct grub_net_buff *
get_card_packet (struct grub_net_card *dev)
{
  grub_efi_simple_network_t *net = dev->efi_net_info->snp_data.snp;
  grub_err_t err;
  grub_efi_status_t st;
  grub_efi_uintn_t bufsize = dev->rcvbufsize;
  struct grub_net_buff *nb;
  int i;

  for (i = 0; i < 2; i++)
    {
      if (!dev->rcvbuf)
	dev->rcvbuf = grub_malloc (dev->rcvbufsize);
      if (!dev->rcvbuf)
	return NULL;

      st = rx (net, NULL, &bufsize, dev->rcvbuf, NULL, NULL, NULL);
      if (st != GRUB_EFI_BUFFER_TOO_SMALL)
	break;
      dev->rcvbufsize = 2 * ALIGN_UP (dev->rcvbufsize > bufsize
				      ? dev->rcvbufsize : bufsize, 64);
      grub_free (dev->rcvbuf);
      dev->rcvbuf = 0;
    }

  if (st != GRUB_EFI_SUCCESS)
    return NULL;

  nb = grub_netbuff_alloc (bufsize + 2);
  if (!nb)
    return NULL;

  /* Reserve 2 bytes so that 2 + 14/18 bytes of ethernet header is divisible
     by 4. So that IP header is aligned on 4 bytes. */
  if (grub_netbuff_reserve (nb, 2))
    {
      grub_netbuff_free (nb);
      return NULL;
    }
  grub_memcpy (nb->data, dev->rcvbuf, bufsize);
  err = grub_netbuff_put (nb, bufsize);
  if (err)
    {
      grub_netbuff_free (nb);
      return NULL;
    }

  return nb;
}

static grub_err_t
open_card (struct grub_net_card *dev)
{
  grub_efi_simple_network_t *net;

  /* Try to reopen SNP exlusively to close any active MNP protocol instance
     that may compete for packet polling
   */
  net = grub_efi_open_protocol (dev->efi_net_info->handle, &snp_io_guid,
				GRUB_EFI_OPEN_PROTOCOL_BY_EXCLUSIVE);
  if (net)
    {
      if (net->mode->state == GRUB_EFI_NETWORK_STOPPED
	  && start(net) != GRUB_EFI_SUCCESS)
	return grub_error (GRUB_ERR_NET_NO_CARD, "%s: net start failed",
			   dev->name);

      if (net->mode->state == GRUB_EFI_NETWORK_STOPPED)
	return grub_error (GRUB_ERR_NET_NO_CARD, "%s: card stopped",
			   dev->name);

      if (net->mode->state == GRUB_EFI_NETWORK_STARTED
	  && initialize (net, 0, 0) != GRUB_EFI_SUCCESS)
	return grub_error (GRUB_ERR_NET_NO_CARD, "%s: net initialize failed",
			   dev->name);

      /* Enable hardware receive filters if driver declares support for it.
	 We need unicast and broadcast and additionaly all nodes and
	 solicited multicast for IPv6. Solicited multicast is per-IPv6
	 address and we currently do not have API to do it so simply
	 try to enable receive of all multicast packets or evertyhing in
	 the worst case (i386 PXE driver always enables promiscuous too).

	 This does trust firmware to do what it claims to do.
       */
      if (net->mode->receive_filter_mask)
	{
	  grub_uint32_t filters = GRUB_EFI_SIMPLE_NETWORK_RECEIVE_UNICAST   |
				  GRUB_EFI_SIMPLE_NETWORK_RECEIVE_BROADCAST |
				  GRUB_EFI_SIMPLE_NETWORK_RECEIVE_PROMISCUOUS_MULTICAST;

	  filters &= net->mode->receive_filter_mask;
	  if (!(filters & GRUB_EFI_SIMPLE_NETWORK_RECEIVE_PROMISCUOUS_MULTICAST))
	    filters |= (net->mode->receive_filter_mask &
			GRUB_EFI_SIMPLE_NETWORK_RECEIVE_PROMISCUOUS);

	  rx_filters (net, filters, 0, 0, 0, NULL);
	}

      grub_efi_close_protocol (net, &snp_io_guid, dev->efi_net_info->handle);
      dev->efi_net_info->snp_data.snp = net;
    }

  /* If it failed we just try to run as best as we can */
  return GRUB_ERR_NONE;
}

static void
close_card (struct grub_net_card *dev)
{
  grub_efi_simple_network_t *snp = dev->efi_net_info->snp_data.snp;

  shutdown (snp);
  stop (snp);

  grub_efi_close_protocol (snp, &snp_io_guid, dev->efi_net_info->handle);
}

struct grub_net_card_driver grub_efi_snp_driver =
  {
    .name = "efinet",
    .open = open_card,
    .close = close_card,
    .send = send_card_buffer,
    .recv = get_card_packet
  };

void
grub_efi_snp_free (struct grub_net_card *dev,
		   grub_efi_snp_data_t *data __attribute__ ((__unused__)))
{
  grub_efi_simple_network_t *snp = dev->efi_net_info->snp_data.snp;
  grub_efi_close_protocol (snp, &snp_io_guid, dev->efi_net_info->handle);
}

void
grub_efi_snp_config_real (struct grub_net_card *card, grub_efi_handle_t hnd,
			  char **device, char **path)
{
  grub_efi_simple_network_t *net;
  grub_efi_net_info_t *info = card->efi_net_info;
  struct grub_efi_pxe *pxe;
  struct grub_efi_pxe_mode *pxe_mode;

  net = grub_efi_open_protocol (info->handle, &snp_io_guid,
				GRUB_EFI_OPEN_PROTOCOL_GET_PROTOCOL);
  if (!net)
    /* This should not happen... Why?  */
    return;

  if (net->mode->state == GRUB_EFI_NETWORK_STOPPED
      && start(net) != GRUB_EFI_SUCCESS)
    return;

  if (net->mode->state == GRUB_EFI_NETWORK_STOPPED)
    return;

  if (net->mode->state == GRUB_EFI_NETWORK_STARTED
      && initialize(net, 0, 0) != GRUB_EFI_SUCCESS)
    return;

  info->snp_data.snp = net;

  info->card->mtu = net->mode->max_packet_size;
  grub_memcpy (info->card->default_address.mac,
	       net->mode->current_address,
	       sizeof (info->card->default_address.mac));

  pxe = grub_efi_open_protocol (hnd, &pxe_io_guid,
				GRUB_EFI_OPEN_PROTOCOL_GET_PROTOCOL);
  if (! pxe)
    return;

  pxe_mode = pxe->mode;
  grub_net_configure_by_dhcp_ack (card->name, card, 0,
				  (struct grub_net_bootp_packet *)
				  &pxe_mode->dhcp_ack,
				  sizeof (pxe_mode->dhcp_ack),
				  1, device, path);

  net = grub_efi_open_protocol (info->handle, &snp_io_guid,
				GRUB_EFI_OPEN_PROTOCOL_BY_EXCLUSIVE);
  if (net)
    {
      if (net->mode->state == GRUB_EFI_NETWORK_STOPPED
	  && start(net) != GRUB_EFI_SUCCESS)
	return;

      if (net->mode->state == GRUB_EFI_NETWORK_STOPPED)
	return;

      if (net->mode->state == GRUB_EFI_NETWORK_STARTED
	  && initialize(net, 0, 0) != GRUB_EFI_SUCCESS)
	return;

      card->efi_net_info->snp_data.snp = net;
    }
}

int
grub_efi_snp_dp_cmp(grub_efi_device_path_t *left, grub_efi_device_path_t *right)
{
#if 0
  grub_printf ("comparing device paths:\n   ");
  grub_efi_print_device_path (left);
  grub_printf("   ");
  grub_efi_print_device_path (right);
#endif

  int cmp = grub_efi_compare_device_paths (left, right);
//  grub_printf("cmp() = %d\n", cmp);
  if (cmp == 0)
    return 0;

  if (cmp != 0)
    {
      grub_efi_device_path_t *ldp, *dup_dp, *dup_ldp;
      int match;

      /* EDK2 UEFI PXE driver creates pseudo devices with type IPv4/IPv6
	 as children of Ethernet card and binds PXE and Load File protocols
	 to it. Loaded Image Device Path protocol will point to these pseudo
	 devices. We skip them when enumerating cards, so here we need to
	 find matching MAC device.
       */
      ldp = grub_efi_find_last_device_path (left);
      if (GRUB_EFI_DEVICE_PATH_TYPE (ldp) != GRUB_EFI_MESSAGING_DEVICE_PATH_TYPE
	  || (GRUB_EFI_DEVICE_PATH_SUBTYPE (ldp) != GRUB_EFI_IPV4_DEVICE_PATH_SUBTYPE
	      && GRUB_EFI_DEVICE_PATH_SUBTYPE (ldp) != GRUB_EFI_IPV6_DEVICE_PATH_SUBTYPE))
	return 1;

      dup_dp = grub_efi_duplicate_device_path (left);
      if (!dup_dp)
	return 1;

      dup_ldp = grub_efi_find_last_device_path (dup_dp);
      dup_ldp->type = GRUB_EFI_END_DEVICE_PATH_TYPE;
      dup_ldp->subtype = GRUB_EFI_END_ENTIRE_DEVICE_PATH_SUBTYPE;
      dup_ldp->length = sizeof (*dup_ldp);
      match = grub_efi_compare_device_paths (dup_dp, right) == 0;
      grub_free (dup_dp);
      if (!match)
	return 1;

    }

  grub_printf ("but it matches anyway!\n");
  return 0;
}

