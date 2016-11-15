/*
 *  GRUB  --  GRand Unified Bootloader
 *  Copyright (C) 2013  Free Software Foundation, Inc.
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

#include <grub/charset.h>
#include <grub/command.h>
#include <grub/err.h>
#include <grub/file.h>
#include <grub/fdt.h>
#include <grub/linux.h>
#include <grub/loader.h>
#include <grub/mm.h>
#include <grub/types.h>
#include <grub/cpu/linux.h>
#include <grub/cpu/fdtload.h>
#include <grub/efi/efi.h>
#include <grub/efi/pe32.h>
#include <grub/i18n.h>
#include <grub/lib/cmdline.h>

static void *kernel_addr;
static grub_uint64_t kernel_size;

static grub_uint32_t entry_point_offset;

static char *linux_args;
static grub_uint32_t cmdline_size;

static grub_addr_t initrd_start;
static grub_addr_t initrd_end;

grub_ssize_t
grub_efi_get_check_image_size (void)
{
  return BYTES_TO_PAGES (sizeof (struct grub_arm64_linux_kernel_header));
}

grub_err_t
grub_efi_check_image (void *kernel, grub_size_t size)
{
  struct grub_arm64_linux_kernel_header *lh =
    (struct grub_arm64_linux_kernel_header *)kernel;

  if (size < sizeof (*lh) || size < lh->hdr_offset ||
      size < lh->hdr_offset + sizeof (struct grub_arm64_linux_pe_header))
    return grub_error(GRUB_ERR_BAD_OS, "kernel too small");

  if (lh->magic != GRUB_ARM64_LINUX_MAGIC)
    return grub_error(GRUB_ERR_BAD_OS, "invalid magic number");

  if ((lh->code0 & 0xffff) != GRUB_EFI_PE_MAGIC)
    return grub_error (GRUB_ERR_NOT_IMPLEMENTED_YET,
		       N_("plain image kernel not supported - rebuild with CONFIG_(U)EFI_STUB enabled"));

  grub_dprintf ("linux", "UEFI stub kernel:\n");
  grub_dprintf ("linux", "text_offset = 0x%012llx\n",
		(long long unsigned) lh->text_offset);
  grub_dprintf ("linux", "PE/COFF header @ %08x\n", lh->hdr_offset);

  return GRUB_ERR_NONE;
}

grub_uint64_t
grub_efi_get_pref_address (void *kernel)
{
  return 0;
}

void
grub_efi_set_up_kernel (void *kernel, grub_uint64_t ksize,
			void *initrd, grub_uint64_t isize,
			void *cmdline)
{
  struct grub_arm64_linux_kernel_header *lh =
    (struct grub_arm64_linux_kernel_header *)kernel;

  struct grub_arm64_linux_pe_header *pe =
    (void *)((grub_uint64_t *)kernel + lh->hdr_offset);

  entry_point_offset = pe->opt.entry_addr;

  kernel_addr = kernel;
  kernel_size = ksize;

  initrd_addr = initrd;
  initrd_size = isize;

  cmdline_addr = cmdline;
}

static grub_err_t
finalize_params_linux (void)
{
  int node, retval;
  grub_efi_loaded_image_t *loaded_image;
  void *fdt;

  fdt = grub_fdt_load (0x400);

  if (!fdt)
    goto failure;

  node = grub_fdt_find_subnode (fdt, 0, "chosen");
  if (node < 0)
    node = grub_fdt_add_subnode (fdt, 0, "chosen");

  if (node < 1)
    goto failure;

  /* Set initrd info */
  if (initrd_start && initrd_end > initrd_start)
    {
      grub_dprintf ("linux", "Initrd @ 0x%012lx-0x%012lx\n",
		    initrd_start, initrd_end);

      retval = grub_fdt_set_prop64 (fdt, node, "linux,initrd-start",
				    initrd_start);
      if (retval)
	goto failure;
      retval = grub_fdt_set_prop64 (fdt, node, "linux,initrd-end",
				    initrd_end);
      if (retval)
	goto failure;
    }

  if (grub_fdt_install() != GRUB_ERR_NONE)
    goto failure;

  /* Convert command line to UCS-2 */
  loaded_image = grub_efi_get_loaded_image (grub_efi_image_handle);
  if (!loaded_image)
    goto failure;

  loaded_image->load_options_size = len =
    (grub_strlen (linux_args) + 1) * sizeof (grub_efi_char16_t);
  loaded_image->load_options =
    grub_efi_allocate_pages (0,
			     GRUB_EFI_BYTES_TO_PAGES (loaded_image->load_options_size));

  if (!loaded_image->load_options)
    goto failure;

  loaded_image->load_options_size =
        2 * grub_utf8_to_utf16 (loaded_image->load_options, len,
				(grub_uint8_t *) linux_args, len, NULL);

  return GRUB_ERR_NONE;

failure:
  grub_fdt_unload();
  return grub_error(GRUB_ERR_BAD_OS, "failed to install/update FDT");
}

static grub_err_t
grub_linux_boot (void)
{
  if (finalize_params_linux () != GRUB_ERR_NONE)
    return grub_errno;

  grub_dprintf ("linux", "starting image %p\n", image_handle);
  return grub_efi_linux_boot (kernel_addr, entry_point_offset, linux_args);
}

static void
grub_linux_unload (void)
{
  grub_efi_linux_unload ();
  grub_fdt_unload ();
}

static grub_err_t
grub_cmd_linux (grub_command_t cmd __attribute__ ((unused)),
		int argc, char *argv[])
{
  grub_file_t file = 0;
  struct grub_arm64_linux_kernel_header lh;

  grub_dl_ref (my_mod);

  if (argc == 0)
    {
      grub_error (GRUB_ERR_BAD_ARGUMENT, N_("filename expected"));
      goto fail;
    }

  file = grub_file_open (argv[0]);
  if (!file)
    goto fail;

  kernel_size = grub_file_size (file);

  if (grub_file_read (file, &lh, sizeof (lh)) < (long) sizeof (lh))
    return grub_errno;

  if (grub_arm64_uefi_check_image (&lh) != GRUB_ERR_NONE)
    goto fail;

  kernel_addr = grub_efi_allocate_pages (0, GRUB_EFI_BYTES_TO_PAGES (kernel_size));
  if (!kernel_addr)
    {
      grub_error (GRUB_ERR_OUT_OF_MEMORY, N_("out of memory"));
      goto fail;
    }

  grub_file_seek (file, 0);
  if (grub_file_read (file, kernel_addr, kernel_size)
      < (grub_int64_t) kernel_size)
    {
      if (!grub_errno)
	grub_error (GRUB_ERR_BAD_OS, N_("premature end of file %s"), argv[0]);
      goto fail;
    }

  grub_dprintf ("linux", "kernel @ %p\n", kernel_addr);

  cmdline_size = grub_loader_cmdline_size (argc, argv) + sizeof (LINUX_IMAGE);
  linux_args = grub_malloc (cmdline_size);
  if (!linux_args)
    {
      grub_error (GRUB_ERR_OUT_OF_MEMORY, N_("out of memory"));
      goto fail;
    }
  grub_memcpy (linux_args, LINUX_IMAGE, sizeof (LINUX_IMAGE));
  grub_create_loader_cmdline (argc, argv,
			      linux_args + sizeof (LINUX_IMAGE) - 1,
			      cmdline_size);

  if (grub_errno == GRUB_ERR_NONE)
    {
      grub_loader_set (grub_linux_boot, grub_linux_unload, 0);
      loaded = 1;
    }

fail:
  if (file)
    grub_file_close (file);

  if (grub_errno != GRUB_ERR_NONE)
    {
      grub_dl_unref (my_mod);
      loaded = 0;
    }

  if (linux_args && !loaded)
    grub_free (linux_args);

  if (kernel_addr && !loaded)
    grub_efi_free_pages ((grub_efi_physical_address_t) kernel_addr,
			 GRUB_EFI_BYTES_TO_PAGES (kernel_size));

  return grub_errno;
}
