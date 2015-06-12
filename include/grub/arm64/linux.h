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

#ifndef GRUB_LINUX_CPU_HEADER
#define GRUB_LINUX_CPU_HEADER 1

#include <grub/efi/efi.h>
#include <grub/efi/pe32.h>

#define GRUB_ARM64_LINUX_MAGIC 0x644d5241 /* 'ARM\x64' */

#define GRUB_EFI_PAGE_SHIFT	12
#define BYTES_TO_PAGES(bytes)   (((bytes) + 0xfff) >> GRUB_EFI_PAGE_SHIFT)
#define GRUB_EFI_PE_MAGIC	0x5A4D

/* From linux/Documentation/arm64/booting.txt */
struct grub_arm64_linux_kernel_header
{
  grub_uint32_t code0;		/* Executable code */
  grub_uint32_t code1;		/* Executable code */
  grub_uint64_t text_offset;    /* Image load offset */
  grub_uint64_t res0;		/* reserved */
  grub_uint64_t res1;		/* reserved */
  grub_uint64_t res2;		/* reserved */
  grub_uint64_t res3;		/* reserved */
  grub_uint64_t res4;		/* reserved */
  grub_uint32_t magic;		/* Magic number, little endian, "ARM\x64" */
  grub_uint32_t hdr_offset;	/* Offset of PE/COFF header */
};

struct grub_arm64_linux_pe_header
{
  grub_uint32_t magic;
  struct grub_pe32_coff_header coff;
  struct grub_pe64_optional_header opt;
};

#endif /* ! GRUB_LINUX_CPU_HEADER */
