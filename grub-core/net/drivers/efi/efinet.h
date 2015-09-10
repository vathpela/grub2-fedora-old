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
#ifndef GRUB_EFINET_H
#define GRUB_EFINET_H

struct grub_efi_mnp_data
{
	grub_efi_managed_network_service_binding_t *sb;
	grub_efi_handle_t handle;
	grub_efi_managed_network_t *mnp;
};
typedef struct grub_efi_mnp_data grub_efi_mnp_data_t;

void EXPORT_FUNC (grub_efi_mnp_free) (struct grub_net_card *dev,
				      grub_efi_mnp_data_t *data);
void EXPORT_FUNC (grub_efi_mnp_config_real) (struct grub_net_card *card
						__attribute__ ((__unused__)),
					     grub_efi_handle_t hnd
						__attribute__ ((__unused__)),
					     char **device
						__attribute__ ((__unused__)),
					     char **path
						__attribute__ ((__unused__)));

struct grub_efi_snp_data
{
	grub_efi_simple_network_t *snp;
	grub_size_t last_pkt_size;
};
typedef struct grub_efi_snp_data grub_efi_snp_data_t;

void EXPORT_FUNC (grub_efi_snp_free) (struct grub_net_card *dev,
		   grub_efi_snp_data_t *data __attribute__ ((__unused__)));
void EXPORT_FUNC (grub_efi_snp_config_real) (struct grub_net_card *card,
					     grub_efi_handle_t hnd,
					     char **device, char **path);
int EXPORT_FUNC (grub_efi_snp_dp_cmp) (grub_efi_device_path_t *left,
				       grub_efi_device_path_t *right);

struct grub_efi_net_info
{
  grub_efi_device_path_t *dp;
  grub_efi_device_path_t *parent;
  grub_efi_device_path_t *child;

  grub_efi_handle_t device_handle;
  grub_efi_handle_t handle;

  struct grub_net_card *card;

  int has_mnp;
  union {
	  grub_efi_mnp_data_t mnp_data;
	  grub_efi_snp_data_t snp_data;
  };
};

struct grub_net_card_driver EXPORT_VAR(grub_efi_snp_driver);
struct grub_net_card_driver EXPORT_VAR(grub_efi_mnp_driver);

#endif
