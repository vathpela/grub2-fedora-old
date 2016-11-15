/*
 *  GRUB  --  GRand Unified Bootloader
 *  Copyright (C) 2014 Free Software Foundation, Inc.
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

#include <grub/command.h>
#include <grub/err.h>
#include <grub/file.h>
#include <grub/i18n.h>
#include <grub/lib/cmdline.h>
#include <grub/linux.h>
#include <grub/loader.h>
#include <grub/mm.h>
#include <grub/types.h>
#include <grub/cpu/linux.h>
#include <grub/efi/efi.h>
#include <grub/efi/linux.h>

GRUB_MOD_LICENSE ("GPLv3+");

static grub_dl_t my_mod;

#define PTR_ADD(ptr, addend) ((__typeof__(ptr))(((grub_intptr_t)(ptr)) + (addend)))

static grub_err_t
grub_efi_linux_unload (void)
{
  grub_dl_unref (my_mod);

  grub_efi_loader_tear_down_kernel ();

  return GRUB_ERR_BUG;
}

static grub_err_t
grub_cmd_initrd (grub_command_t cmd __attribute__ ((unused)),
		 int argc, char *argv[])
{
  struct grub_linux_initrd_context initrd_ctx = { 0, 0, 0 };
  int initrd_size = 0;
  void *initrd_addr = NULL;
  grub_efi_uint64_t initrd_max;

  if (argc == 0)
    {
      grub_error (GRUB_ERR_BAD_ARGUMENT, N_("filename expected"));
      goto fail;
    }

  if (grub_initrd_init (argc, argv, &initrd_ctx))
    goto fail;

  initrd_max = grub_efi_loader_get_max_initrd_address ();
  if (initrd_max == 0)
    goto fail;

  initrd_size = grub_get_initrd_size (&initrd_ctx);

  grub_dprintf ("linux", "Loading initrd\n");

  initrd_addr = grub_efi_allocate_pages_max (initrd_max,
					    BYTES_TO_PAGES (initrd_size));
  if (!initrd_addr)
    {
      grub_error (GRUB_ERR_OUT_OF_MEMORY, N_("out of memory"));
      goto fail;
    }

  if (grub_initrd_load (&initrd_ctx, argv, initrd_addr))
    goto fail;

  grub_dprintf ("linux", "[addr=%p, size=0x%x]\n", initrd_addr, initrd_size);

  grub_efi_loader_set_up_initrd (initrd_addr, initrd_size);

fail:
  grub_initrd_close (&initrd_ctx);

  if (grub_errno != GRUB_ERR_NONE && initrd_addr)
    grub_efi_free_pages ((grub_efi_physical_address_t) initrd_addr,
			 BYTES_TO_PAGES (initrd_size));

  return grub_errno;
}

static grub_err_t
grub_cmd_linux (grub_command_t cmd __attribute__ ((unused)),
		int argc, char *argv[])
{
  grub_file_t file = 0;
  int loaded;
  grub_ssize_t check_size, kernel_size, len;
  grub_uint64_t pref_address, kernel_max = 0;
  void *check = NULL, *kernel = NULL;
  grub_efi_physical_address_t kernel_buf = 0;
  grub_efi_uintn_t kernel_pages = 0;

  grub_dl_ref (my_mod);

  if (argc == 0)
    {
      grub_error (GRUB_ERR_BAD_ARGUMENT, N_("filename expected"));
      goto fail;
    }

  file = grub_file_open (argv[0]);
  if (! file)
    goto fail;

  kernel_size = grub_file_size (file);
  if ((grub_size_t)kernel_size == GRUB_FILE_SIZE_UNKNOWN)
    {
      grub_error (GRUB_ERR_FILE_READ_ERROR, N_("Unknown size for kernel"));
      goto fail;
    }

  check_size = grub_efi_loader_get_check_image_size ();
  check = grub_malloc (check_size);
  if (!check)
    {
      grub_error (GRUB_ERR_OUT_OF_MEMORY, N_("cannot allocate kernel buffer"));
      goto fail;
    }

  if (grub_file_read (file, check, check_size) != check_size)
    {
      grub_error (GRUB_ERR_FILE_READ_ERROR, N_("Can't read kernel %s"),
		  argv[0]);
      goto fail;
    }

  if (grub_efi_loader_check_image (kernel, kernel_size) != GRUB_ERR_NONE)
    goto fail;

  /* try to allocate the right amount of space at our optimal load address */
  pref_address = grub_efi_loader_get_pref_address (check);
  kernel_pages = BYTES_TO_PAGES (kernel_size);

  kernel = grub_efi_allocate_pages (pref_address, kernel_pages);
  kernel_buf = (grub_efi_physical_address_t)kernel;

  /* if we couldn't get the preferred load address, try to get an address that
   * has the alignment we want */
  if (!kernel)
    {
      grub_efi_uint32_t kernel_alignment;

      kernel_max = grub_efi_loader_get_max_address (check);

      kernel_alignment = grub_efi_loader_get_kernel_alignment (check);
      kernel = grub_efi_allocate_aligned_max (kernel_max,
					      kernel_size, kernel_alignment,
					      &kernel_buf, &kernel_pages);
    }

  /* if that *still* didn't work, try to get some pages with our bare minimal
   * alignment... */
  if (!kernel)
    {
      grub_efi_uint8_t min_alignment;

      min_alignment = grub_efi_loader_get_min_kernel_alignment (check);
      kernel = grub_efi_allocate_aligned_max (kernel_max,
					      kernel_size, min_alignment,
					      &kernel_buf, &kernel_pages);
    }

  /* or else we just lose */
  if (!kernel)
    goto fail;

  grub_memcpy (kernel, check, check_size);

  grub_dprintf ("linux", "kernel file size: %lld\n", (long long) kernel_size);
  grub_dprintf ("linux", "kernel numpages: %lld\n",
		(long long) BYTES_TO_PAGES (kernel_size));

  len = kernel_size - check_size;
  if (grub_file_read (file, PTR_ADD(kernel, check_size), len) != len)
    {
      grub_error (GRUB_ERR_FILE_READ_ERROR, N_("Can't read kernel %s"),
		  argv[0]);
      goto fail;
    }

  grub_loader_unset();

  grub_efi_loader_set_up_kernel (kernel, kernel_size,
				 kernel_buf, kernel_pages,
				 argc, argv);

  grub_loader_set (grub_efi_loader_linux_boot, grub_efi_linux_unload, 0);
  loaded=1;

fail:
  if (grub_errno != GRUB_ERR_NONE)
    {
      grub_dl_unref (my_mod);
      loaded = 0;
    }

  if (!loaded)
    {
      grub_efi_loader_tear_down_kernel ();

      if (kernel_buf)
	grub_efi_free_pages (kernel_buf, kernel_pages);
    }

  if (check)
    grub_free (check);

  if (file)
    grub_file_close (file);

  return grub_errno;
}

static grub_command_t cmd_linux, cmd_initrd;

GRUB_MOD_INIT(linuxefi)
{
  cmd_linux =
    grub_register_command ("linux", grub_cmd_linux,
                           0, N_("Load Linux."));
  cmd_initrd =
    grub_register_command ("initrd", grub_cmd_initrd,
                           0, N_("Load initrd."));

  my_mod = mod;
}

GRUB_MOD_FINI(linuxefi)
{
  grub_unregister_command (cmd_linux);
  grub_unregister_command (cmd_initrd);
}
