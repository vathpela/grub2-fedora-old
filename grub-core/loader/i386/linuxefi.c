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

#include <grub/err.h>
#include <grub/lib/cmdline.h>
#include <grub/loader.h>
#include <grub/mm.h>
#include <grub/types.h>
#include <grub/cpu/linux.h>
#include <grub/efi/efi.h>
#include <grub/efi/linux.h>

#define PTR_ADD(ptr, addend) ((__typeof__(ptr))(((grub_intptr_t)(ptr)) + (addend)))

struct kernel_context
{
  int loaded;

  void *kernel_addr;
  grub_efi_uint64_t kernel_size;

  grub_efi_physical_address_t kernel_buf;
  grub_efi_uintn_t kernel_pages;

  grub_efi_physical_address_t initrd_addr;
  grub_efi_uint64_t initrd_size;

  grub_efi_physical_address_t cmdline_addr;
  grub_efi_uint64_t cmdline_size;

  grub_efi_uint32_t entry_point_offset;

  struct linux_kernel_params *params;
  grub_size_t params_size;
};

static struct kernel_context ctx;

grub_ssize_t
grub_efi_loader_get_check_image_size (void)
{
  return sizeof (struct linux_kernel_params);
}

grub_err_t
grub_efi_loader_check_image (void *kernel, grub_size_t kernel_size)
{
  struct linux_kernel_header *lh = (struct linux_kernel_header *)kernel;
  int offset = 0;

  if (kernel_size < sizeof (*lh))
    return grub_error(GRUB_ERR_BAD_OS, "kernel too small");

#ifdef __x86_64__
  offset = 0x200;

  if (!(lh->xloadflags & LINUX_XLF_KERNEL_64) ||
      !(lh->xloadflags & LINUX_XLF_EFI_HANDOVER_64))
    return grub_error (GRUB_ERR_BAD_OS,
		       N_("kernel doesn't support EFI handover"));
#else
  if (!(lh->xloadflags & LINUX_XLF_KERNEL_32))
    return grub_error (GRUB_ERR_BAD_OS,
		       N_("kernel doesn't support EFI handover"));
#endif

  if (!lh->handover_offset)
    return grub_error (GRUB_ERR_BAD_OS,
		       N_("kernel doesn't support EFI handover"));

  if (lh->boot_flag != grub_cpu_to_le16 (0xaa55))
    return grub_error (GRUB_ERR_BAD_OS, N_("kernel has invalid magic number"));

  if (lh->setup_sects > GRUB_LINUX_MAX_SETUP_SECTS)
    return grub_error (GRUB_ERR_BAD_OS,
		       N_("kernel has too many setup sectors"));

  if (lh->version < grub_cpu_to_le16 (0x020b))
    return grub_error (GRUB_ERR_BAD_OS, N_("kernel is too old"));

  grub_dprintf ("linux", "UEFI stub kernel:\n");
  grub_dprintf ("linux", "handover_offset = 0x%08lx\n",
		(unsigned long)lh->handover_offset + offset);

  return GRUB_ERR_NONE;
}

grub_intptr_t
grub_efi_loader_get_pref_address (struct linux_kernel_params *params)
{
  return params->pref_address;
}

grub_intptr_t
grub_efi_loader_get_max_kernel_address (struct linux_kernel_params *params
					__attribute__ ((unused)))
{
  grub_intptr_t addr = 0xffffffffUL & ~((unsigned long)PAGE_SIZE - 1);

  /* Currently the kernel supports XLF_CAN_BE_LOADED_ABOVE_4G, and
   * pref_address is 64-bit, but code32_start is 32-bit.  If the two don't
   * match, the kernel automatically relocates itself.  So basically, any time
   * we're above 4G, we just get a memcpy() for no reason.
   *
   * So avoid it until we can get ext_code32_start added.
   */
#if 0 && defined(__x86_64__)
  if (params->xloadflags & LINUX_XLF_CAN_BE_LOADED_ABOVE_4G)
    addr = GRUB_EFI_PHYSICAL_ADDRESS_MAX & ~((unsigned long long)PAGE_SIZE - 1);
#endif
  return addr;
}

grub_ssize_t
grub_efi_loader_get_kernel_alignment (struct linux_kernel_params *params)
{
  return params->kernel_alignment;
}

grub_ssize_t
grub_efi_loader_get_min_kernel_alignment (struct linux_kernel_params *params)
{
  return 1 << params->min_alignment;
}

grub_intptr_t
grub_efi_loader_get_max_cmdline_address (struct linux_kernel_params *params)
{
#ifdef __x86_64__
  if (params->xloadflags & LINUX_XLF_CAN_BE_LOADED_ABOVE_4G)
    return GRUB_EFI_PHYSICAL_ADDRESS_MAX & ~((unsigned long long)PAGE_SIZE - 1);
#endif
  return 0xfffffffful & ~((unsigned long)PAGE_SIZE - 1);
}

grub_ssize_t
grub_efi_loader_get_max_cmdline_size (struct linux_kernel_params *params)
{
  return params->cmdline_size;
}

grub_intptr_t
grub_efi_loader_get_max_initrd_address (void)
{
#ifdef __x86_64__
  if (params->xloadflags & LINUX_XLF_CAN_BE_LOADED_ABOVE_4G)
    return GRUB_EFI_PHYSICAL_ADDRESS_MAX & ~((unsigned long long)PAGE_SIZE - 1);
#endif

  return ctx.params->initrd_addr_max & ~((unsigned long)PAGE_SIZE - 1);
}

void
grub_efi_loader_tear_down_kernel (void)
{
  if (!ctx.loaded)
    return;

  if (ctx.params)
    grub_efi_free_pages ((grub_efi_physical_address_t)ctx.params,
			 BYTES_TO_PAGES(ctx.params_size));

  if (ctx.kernel_buf)
    grub_efi_free_pages (ctx.kernel_buf, ctx.kernel_pages);

  if (ctx.initrd_addr)
    grub_efi_free_pages (ctx.initrd_addr, BYTES_TO_PAGES(ctx.initrd_size));

  if (ctx.cmdline_addr)
    grub_efi_free_pages (ctx.cmdline_addr, BYTES_TO_PAGES(ctx.cmdline_size));

  grub_memset (&ctx, 0, sizeof (ctx));
}

static grub_err_t
grub_efi_loader_set_up_params (void *kernel, grub_size_t kernel_size)
{
  /* 0x0202 and 0x0201 are explicitly how this is documented in
   * Documentation/x86/boot.txt it's the bottom half of the jump to code
   * instruction, so there's not a named field per se. */
  if (kernel_size < 0x0202)
too_small:
    return grub_error(GRUB_ERR_BAD_OS, "kernel too small");

  ctx.params_size = ((grub_int8_t *)kernel)[0x0201] + 0x0202;
  if (kernel_size < params_size)
    goto too_small;

  ctx.params = grub_efi_allocate_pages (0, BYTES_TO_PAGES(ctx.params_size));

  if (!ctx.params)
    {
      ctx.params_size = 0;
      return grub_error (GRUB_ERR_OUT_OF_MEMORY, N_("out of memory"));
    }

  grub_memcpy (ctx.params + 0x1f1, kernel + 0x1f1, ctx.params_size - 0x1f1);

  return GRUB_ERR_NONE;
}

static grub_err_t
set_up_cmdline (int argc, char *argv[])
{
  grub_intptr_t max_cmdline_addr;
  grub_size_t max_cmdline_size;
  grub_size_t cmdline_size;
  char *cmdline;

  /* ->cmdline_size is the max size of a command line /not including/ the NUL
   * termination. */
  max_cmdline_addr = grub_efi_loader_get_max_cmdline_address (params_in);
  max_cmdline_size = params_in->cmdline_size + 1;
  cmdline_size = grub_loader_cmdline_size (argc, argv);
  if (cmdline_size > max_cmdline_size)
    cmdline_size = max_cmdline_size;

  cmdline =
    grub_efi_allocate_pages_max (max_cmdline_addr,
				 BYTES_TO_PAGES(cmdline_size));
  if (!cmdline)
    {
      grub_error (GRUB_ERR_OUT_OF_MEMORY, N_("can't allocate cmdline"));
      goto fail;
    }

  grub_memcpy (cmdline, LINUX_IMAGE, sizeof (LINUX_IMAGE));
  grub_create_loader_cmdline (argc, argv,
                              cmdline + sizeof (LINUX_IMAGE) - 1,
			      params_in->cmdline_size
			      - (sizeof (LINUX_IMAGE) - 1));



grub_err_t
grub_efi_loader_set_up_kernel (void *kernel, grub_efi_uint64_t ksize,
			       grub_efi_physical_address_t kernel_buf,
			       grub_efi_uintn_t kernel_pages,
			       int argc, char *argv[])
{
  struct linux_kernel_params *params_in = kernel;

  if (ctx.loaded)
    grub_efi_loader_tear_down_kernel ();

  set_up_params (kernel, ksize);
  if (grub_errno != GRUB_ERR_NONE)
    return grub_errno;

  set_up_cmdline ();
  if (grub_errno != GRUB_ERR_NONE)
    return grub_errno;

#ifdef __x86_64__
  /* The x86_64 64-bit EFI STUB entry point is offset 0x200 from the
   * handover_offset. */
  ctx.entry_point_offset = params_in->handover_offset + 0x200;
#else
  ctx.entry_point_offset = params_in->handover_offset;
#endif

  ctx.kernel_addr = kernel;
  ctx.kernel_size = ksize;
  ctx.kernel_buf = kernel_buf;
  ctx.kernel_pages = kernel_pages;

  ctx.params->code32_start = (grub_uint32_t)(grub_intptr_t)kernel;

  ctx.params->type_of_loader = GRUB_LINUX_BOOT_LOADER_TYPE;
  ctx.params->ext_loader_ver = 0x02;

  ctx.params->cmd_line_ptr = (grub_uint32_t)(grub_uint64_t)cmdline;

  grub_memcpy (params_in, ctx.params, 2 * 512);

  return GRUB_ERR_NONE;
fail:

  grub_free (ctx.params);
  ctx.params = 0;

  return grub_errno;
}

grub_err_t
grub_efi_loader_set_up_initrd (void *initrd_addr, grub_size_t initrd_size)
{
  if (!ctx.loaded)
    {
      grub_error (GRUB_ERR_BAD_ARGUMENT,
		  N_("you need to load the kernel first"));
      goto fail;
    }

  ctx.initrd_addr = (grub_efi_physical_address_t)initrd_addr;
  ctx.initrd_size = initrd_size;

  ctx.params->ramdisk_image = (grub_uint32_t)(grub_uint64_t) initrd_addr;
  ctx.params->ramdisk_size = initrd_size;

  return GRUB_ERR_NONE;

fail:
  ctx.initrd_addr = 0;
  ctx.initrd_size = 0;
  ctx.params->ramdisk_image = 0;
  ctx.params->ramdisk_size = 0;

  return grub_errno;
}

typedef void (*handover_func) (void *, grub_efi_system_table_t *, void *);

grub_err_t
grub_efi_loader_linux_boot (void)
{
  grub_dprintf ("linux", "starting image %p\n", ctx.kernel_addr);
  handover_func hf;

  void *kernel_start = PTR_ADD(ctx.kernel_addr,
			       ((ctx.params->setup_sects + 1) * 512));

  hf = (handover_func)PTR_ADD(kernel_start,ctx.entry_point_offset);
  asm volatile ("cli");

  hf (grub_efi_image_handle, grub_efi_system_table, ctx.params);

  /* sure, this will all work out, why not... */
  asm volatile ("cli");
  return GRUB_ERR_BUG;
}
