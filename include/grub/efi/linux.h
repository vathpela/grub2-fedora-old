/* linux.h - declare variables and functions for Linux EFI stub support */
/*
 *  GRUB  --  GRand Unified Bootloader
 *  Copyright (C) 2006,2007,2008,2009  Free Software Foundation, Inc.
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
#ifndef GRUB_EFI_LINUX_HEADER
#define GRUB_EFI_LINUX_HEADER 1

/* these are per-arch loader functions used by the generic efi loader */
/* these get registered with grub_set_loader() */
grub_err_t
grub_efi_loader_linux_boot (void);

/* these are per-arch helpers they use */
/* helpers to examine the parameters of the kernel/system */
grub_ssize_t
grub_efi_loader_get_check_image_size (void);

grub_intptr_t
grub_efi_loader_get_pref_address (struct linux_kernel_params *params);

grub_intptr_t
grub_efi_loader_get_max_address (struct linux_kernel_params *params
			    __attribute__ ((unused)));

grub_ssize_t
grub_efi_loader_get_kernel_alignment (struct linux_kernel_params *params);

grub_ssize_t
grub_efi_loader_get_min_kernel_alignment (struct linux_kernel_params *params);

grub_intptr_t
grub_efi_loader_get_max_cmdline_address (struct linux_kernel_params *params);

grub_ssize_t
grub_efi_loader_get_max_cmdline_size (struct linux_kernel_params *params);

grub_intptr_t
grub_efi_loader_get_max_initrd_address (void);

/* helpers to perform tasks */
grub_err_t
grub_efi_loader_check_image (void *kernel, grub_size_t kernel_size);

grub_err_t
grub_efi_loader_set_up_params (void *kernel, grub_size_t kernel_size);

grub_err_t
grub_efi_loader_set_up_kernel (void *kernel, grub_efi_uint64_t ksize,
				 grub_efi_physical_address_t kernel_buf,
				 grub_efi_uintn_t kernel_pages,
				 int argc, char *argv[]);

void
grub_efi_loader_tear_down_kernel (void);

grub_err_t
grub_efi_loader_set_up_initrd (void *initrd_addr, grub_size_t initrd_size);

#endif /* ! GRUB_EFI_LINUX_HEADER */
