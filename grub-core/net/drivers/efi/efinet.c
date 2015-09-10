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
static grub_efi_guid_t mnpsb_guid = GRUB_EFI_MNP_SERVICE_BINDING_PROTOCOL_GUID;
static grub_efi_guid_t mnp_io_guid = GRUB_EFI_MANAGED_NETWORK_GUID;
static grub_efi_guid_t snp_io_guid = GRUB_EFI_SIMPLE_NETWORK_GUID;

static void
set_watchdog_timer (grub_efi_uintn_t timeout)
{
  efi_call_4 (grub_efi_system_table->boot_services->set_watchdog_timer,
	      timeout, 0, 0, NULL);
}

grub_efi_handle_t
grub_efinet_get_device_handle (struct grub_net_card *card)
{
  if (!card || (card->driver != &grub_efi_snp_driver &&
		card->driver != &grub_efi_mnp_driver))
    return 0;
  return card->efi_net_info->handle;
}

static void
free_info (grub_efi_net_info_t *info)
{
  if (!info)
    return;

  if (info->dp)
    grub_free (info->dp);

  if (info->has_mnp)
      grub_efi_mnp_free (info->card, &info->mnp_data);
  else
      grub_efi_snp_free (info->card, &info->snp_data);

  if (info->card)
    grub_free (info->card);

  grub_free (info);
}

static grub_efi_net_info_t **net_info_list;
static int num_net_infos;

static int
add_info_to_list (grub_efi_net_info_t *info)
{
  grub_efi_net_info_t **new_net_info_list;

  new_net_info_list = grub_zalloc ((num_net_infos + 1) *
				   sizeof (grub_efi_net_info_t *));
  if (!new_net_info_list)
    return -1;

  grub_memcpy (new_net_info_list, net_info_list,
	       (num_net_infos) * sizeof (grub_efi_net_info_t *));
  new_net_info_list[num_net_infos++] = info;
  grub_free (net_info_list);
  net_info_list = new_net_info_list;
  return 0;
}

static int
grub_efinet_find_mnp_cards (grub_efi_handle_t preferred)
{
  grub_efi_uintn_t num_handles = 0;
  grub_efi_handle_t *handles = NULL;
  grub_efi_handle_t *handle;
  grub_efi_net_info_t *info = NULL;
  int i = 0;
  int ret = 0;

  handles = grub_efi_locate_handle (GRUB_EFI_BY_PROTOCOL, &mnpsb_guid,
				    0, &num_handles);

  for (handle = handles; handle && num_handles--; handle++)
    {
      grub_efi_device_path_t *dp, *parent = NULL, *child = NULL;
      grub_efi_status_t status;

      grub_printf ("got mnp handle %p ", *handle);
      if (preferred != NULL && *handle != preferred)
	{
	  grub_printf ("- skipping\n");
	  continue;
	}
      grub_printf("\n");

      dp = grub_efi_get_device_path (*handle);
      if (!dp)
	continue;

      grub_printf("efinet mnpsb: ");
      grub_efi_print_device_path(dp);

      info = grub_zalloc (sizeof (grub_efi_net_info_t));
      if (!info)
	{
err_ret:
	  grub_print_error ();
	  free_info (info);
	  grub_free (handles);
	  return -1;
	}

      info->dp = grub_efi_duplicate_device_path (dp);
      if (!info->dp)
	goto err_ret;

      for (; ! GRUB_EFI_END_ENTIRE_DEVICE_PATH (dp);
	   dp = GRUB_EFI_NEXT_DEVICE_PATH (dp))
	{
	  parent = child;
	  child = dp;
	}
#if 0
      if (child
	  && GRUB_EFI_DEVICE_PATH_TYPE (child) ==
	     GRUB_EFI_MESSAGING_DEVICE_PATH_TYPE
	  && (GRUB_EFI_DEVICE_PATH_SUBTYPE (child) ==
	      GRUB_EFI_IPV4_DEVICE_PATH_SUBTYPE
	      || GRUB_EFI_DEVICE_PATH_SUBTYPE (child) ==
	         GRUB_EFI_IPV6_DEVICE_PATH_SUBTYPE)
	  && parent
	  && GRUB_EFI_DEVICE_PATH_TYPE (parent) ==
	     GRUB_EFI_MESSAGING_DEVICE_PATH_TYPE
	     && GRUB_EFI_DEVICE_PATH_SUBTYPE (parent) ==
	        GRUB_EFI_MAC_ADDRESS_DEVICE_PATH_SUBTYPE)
	continue;
#endif

      if (parent)
	{
	  info->parent = grub_efi_duplicate_device_path (parent);
	  if (!info->parent)
	    goto err_ret;
	}

      if (child)
	{
	  info->child = grub_efi_duplicate_device_path (child);
	  if (!info->child)
	    goto err_ret;
	}

      info->handle = *handle;

      info->card = grub_zalloc (sizeof (struct grub_net_card));
      if (!info->card)
	goto err_ret;

      info->card->efi_net_info = info;
      info->card->name = grub_xasprintf ("efinet%d", i++);
      info->card->driver = &grub_efi_mnp_driver;

      info->has_mnp = 1;
      info->mnp_data.sb = grub_efi_open_protocol (info->handle, &mnpsb_guid,
					 GRUB_EFI_OPEN_PROTOCOL_GET_PROTOCOL);
      grub_printf("info->mnp_data.sb: %p\n", info->mnp_data.sb);
      if (!info->mnp_data.sb)
	{
err_next_mnp:
	  free_info (info);
	  continue;
	}

      status = grub_efi_create_child (info->mnp_data.sb,
				      &info->mnp_data.handle);
      grub_printf("status: %ld info->mnp_data.handle: %p\n", status & 0x7fffffffffffffff,
		  info->mnp_data.handle);
      if (status != GRUB_EFI_SUCCESS)
	goto err_next_mnp;

      info->mnp_data.mnp = grub_efi_open_protocol (info->mnp_data.handle,
						   &mnp_io_guid,
					  GRUB_EFI_OPEN_PROTOCOL_GET_PROTOCOL);
      grub_printf("info->mnp_data.mnp: %p\n", info->mnp_data.mnp);
      if (!info->mnp_data.mnp)
	goto err_next_mnp;

      if (add_info_to_list(info) < 0)
	goto err_ret;

      ret = 1;
      if (preferred)
	break;
    }

  grub_free (handles);

  return ret;
}

static int
grub_efinet_find_snp_cards (grub_efi_handle_t preferred)
{
  grub_efi_uintn_t num_handles = 0;
  grub_efi_handle_t *handles = NULL;
  grub_efi_handle_t *handle;
  grub_efi_net_info_t *info = NULL;
  int i = 0, j;
  int ret = 0;

  handles = grub_efi_locate_handle (GRUB_EFI_BY_PROTOCOL, &snp_io_guid,
				    0, &num_handles);

  for (handle = handles; handle && num_handles--; handle++)
    {
      grub_efi_device_path_t *dp, *parent = NULL, *child = NULL, *copy;
      int exists;

      grub_printf ("got snp handle %p ", *handle);
      if (preferred != NULL && *handle != preferred)
	{
	  grub_printf ("- skipping\n");
	  continue;
	}
      grub_printf("\n");

      dp = grub_efi_get_device_path (*handle);
      if (!dp)
	{
	  grub_printf ("couldn't get dp?\n");
	  continue;
	}

      copy = grub_efi_duplicate_device_path(dp);
      if (!copy)
	{
err_ret:
	  grub_print_error ();
	  free_info (info);
	  grub_free (handles);
	  return -1;
	}

      for (; ! GRUB_EFI_END_ENTIRE_DEVICE_PATH (dp);
	   dp = GRUB_EFI_NEXT_DEVICE_PATH (dp))
	{
	  parent = child;
	  child = dp;
	}
      if (preferred == NULL && child
	  && GRUB_EFI_DEVICE_PATH_TYPE (child) ==
	     GRUB_EFI_MESSAGING_DEVICE_PATH_TYPE
	  && (GRUB_EFI_DEVICE_PATH_SUBTYPE (child) ==
	      GRUB_EFI_IPV4_DEVICE_PATH_SUBTYPE
	      || GRUB_EFI_DEVICE_PATH_SUBTYPE (child) ==
	         GRUB_EFI_IPV6_DEVICE_PATH_SUBTYPE)
	  && parent
	  && GRUB_EFI_DEVICE_PATH_TYPE (parent) ==
	     GRUB_EFI_MESSAGING_DEVICE_PATH_TYPE
	     && GRUB_EFI_DEVICE_PATH_SUBTYPE (parent) ==
	        GRUB_EFI_MAC_ADDRESS_DEVICE_PATH_SUBTYPE)
	{
#if 1
	  grub_printf (" parent: ");
	  grub_efi_print_device_path (parent);
	  grub_printf ("  child: ");
	  grub_efi_print_device_path (child);
#endif
	  grub_printf ("excluded for wacky dp reasons\n");
	  continue;
	}

      exists = 0;
      for (j = 0; j < num_net_infos; j++)
	{
	  if (*handle == net_info_list[j]->handle)
	    {
	      grub_printf("handle is already registered, skipping.\n");
	      exists = 1;
	      break;
	    }

	  if (!grub_efi_snp_dp_cmp(copy, net_info_list[j]->dp))
	    {
	      grub_printf ("dp is already registered, skipping.\n");
#if 1
	      exists = 1;
	      break;
#endif
	    }

	  if (!grub_efi_snp_dp_cmp(child, net_info_list[j]->child))
	    {
	      grub_printf ("child is already registered, skipping.\n");
#if 1
	      exists = 1;
	      break;
#endif
	    }

	  if (!grub_efi_snp_dp_cmp(parent, net_info_list[j]->child))
	    {
	      grub_printf ("parent is already registered, skipping.\n");
#if 1
	      exists = 1;
	      break;
#endif
	    }

	}
      if (exists)
	{
	  grub_free (copy);
	  continue;
	}

      grub_printf("efinet snp: ");
      grub_efi_print_device_path (copy);

      info = grub_zalloc (sizeof (grub_efi_net_info_t));
      if (!info)
	{
	  grub_free (copy);
	  goto err_ret;
	}

      info->dp = copy;

      if (parent)
	{
	  info->parent = grub_efi_duplicate_device_path (parent);
	  if (!info->parent)
	    goto err_ret;
	}

      if (child)
	{
	  info->child = grub_efi_duplicate_device_path (child);
	  if (!info->child)
	    goto err_ret;
	}

      info->handle = *handle;

      info->card = grub_zalloc (sizeof (struct grub_net_card));
      if (!info->card)
	goto err_ret;

      info->card->efi_net_info = info;
      info->card->name = grub_xasprintf ("efinet%d", i++);
      info->card->driver = &grub_efi_snp_driver;

#if 0
      info->card->mtu = snp->mode->max_packet_size;
      grub_memcpy (info->card->default_address.mac,
		   snp->mode->current_address,
		   sizeof (info->card->default_address.mac));
#endif

      if (add_info_to_list(info) < 0)
	goto err_ret;

      ret = 1;
      if (preferred)
	break;
    }
  grub_free (handles);

  return ret;
}

static void
grub_efinet_findcards (void)
{
  grub_efi_loaded_image_t *image = NULL;
  grub_efi_net_info_t *info = NULL;
  int i;
  int rc;

  set_watchdog_timer (30);

  image = grub_efi_get_loaded_image (grub_efi_image_handle);
  grub_printf ("image->device_handle: %p\n", image->device_handle);

  /* EDK2 UEFI PXE driver creates IPv4 and IPv6 messaging devices as
     children of main MAC messaging device. We only need one device with
     bound MNP or SNP per physical card, otherwise they compete with each
     other when polling for incoming packets.  So find all of both and
     corelate them as appropriate. */
  rc = grub_efinet_find_mnp_cards (image->device_handle);
  if (rc == 0)
    rc = grub_efinet_find_snp_cards (image->device_handle);
  if (rc < 0)
    return;

  rc = grub_efinet_find_mnp_cards (NULL);
  if (rc < 0)
    return;

  rc = grub_efinet_find_snp_cards (NULL);
  if (rc < 0)
    return;

  for (i = 0; i < num_net_infos; i++)
    {
      info = net_info_list[i];

      grub_printf("registering %s\n", info->card->name);
      grub_net_card_register (info->card);
    }
}

static void
grub_efi_net_config_real (grub_efi_handle_t hnd, char **device,
			  char **path)
{
  struct grub_net_card *card;
  grub_efi_device_path_t *dp;

  dp = grub_efi_get_device_path (hnd);
  if (! dp)
    return;

  FOR_NET_CARDS (card)
  {
    grub_efi_device_path_t *cdp;
    if (card->driver != &grub_efi_snp_driver &&
	card->driver != &grub_efi_mnp_driver)
      continue;
    cdp = grub_efi_get_device_path (card->efi_net_info->handle);
    if (! cdp)
      continue;

    if (grub_efi_snp_dp_cmp(dp, cdp))
      continue;

    if (card->driver == &grub_efi_snp_driver)
      {
	grub_printf("running snp_config_real()\n");
	grub_efi_snp_config_real (card, hnd, device, path);
      }
    else if (card->driver == &grub_efi_mnp_driver)
      {
	grub_printf("running mnp_config_real()\n");
	grub_efi_mnp_config_real (card, hnd, device, path);
      }
  }
}

GRUB_MOD_INIT(efinet)
{
  grub_efinet_findcards ();
  grub_efi_net_config = grub_efi_net_config_real;
}

GRUB_MOD_FINI(efinet)
{
  struct grub_net_card *card, *next;

  FOR_NET_CARDS_SAFE (card, next)
    if (card->driver == &grub_efi_snp_driver ||
	card->driver == &grub_efi_mnp_driver)
      grub_net_card_unregister (card);
}
