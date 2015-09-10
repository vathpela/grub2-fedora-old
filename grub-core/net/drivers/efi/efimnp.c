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

struct grub_net_card_driver grub_efi_mnp_driver =
  {
    .name = "efinet",
#if 0
    .send = send_card_buffer,
    .recv = get_card_packet,
#endif
  };

void
grub_efi_mnp_free (struct grub_net_card *dev, grub_efi_mnp_data_t *data)
{
  grub_efi_close_protocol (data->mnp, &mnp_io_guid, data->handle);
  grub_efi_destroy_child (data->sb, &data->handle);
  grub_efi_close_protocol (data->sb, &mnpsb_guid, dev->efi_net_info->handle);
}

int
grub_efi_mnp_dp_cmp(grub_efi_device_path_t *left, grub_efi_device_path_t *right)
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

void
grub_efi_mnp_config_real (struct grub_net_card *card
						__attribute__ ((__unused__)),
			  grub_efi_handle_t hnd __attribute__ ((__unused__)),
			  char **device __attribute__ ((__unused__)),
			  char **path __attribute__ ((__unused__)))
{
	return;
}
