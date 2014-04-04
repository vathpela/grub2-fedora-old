/* compiler-rt.h - prototypes for compiler helpers. */
/*
 *  GRUB  --  GRand Unified Bootloader
 *  Copyright (C) 2002,2003,2005,2006,2007,2008,2009,2010-2014  Free Software Foundation, Inc.
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

#ifndef GRUB_COMPILER_RT_HEADER
#define GRUB_COMPILER_RT_HEADER	1

#include <stdarg.h>
#include <grub/types.h>
#include <grub/symbol.h>

#if defined (__arm__)

grub_uint32_t
EXPORT_FUNC (__udivsi3) (grub_uint32_t a, grub_uint32_t b);

grub_uint32_t
EXPORT_FUNC (__umodsi3) (grub_uint32_t a, grub_uint32_t b);

#endif

#if defined (__sparc__) || defined (__powerpc__)
unsigned
EXPORT_FUNC (__ctzdi2) (grub_uint64_t x);
#define NEED_CTZDI2 1
#endif

#if defined (__mips__) || defined (__arm__)
unsigned
EXPORT_FUNC (__ctzsi2) (grub_uint32_t x);
#define NEED_CTZSI2 1
#endif

#ifdef __arm__
grub_uint32_t
EXPORT_FUNC (__aeabi_uidiv) (grub_uint32_t a, grub_uint32_t b);
grub_uint32_t
EXPORT_FUNC (__aeabi_uidivmod) (grub_uint32_t a, grub_uint32_t b);

/* Needed for allowing modules to be compiled as thumb.  */
grub_uint64_t
EXPORT_FUNC (__muldi3) (grub_uint64_t a, grub_uint64_t b);
grub_uint64_t
EXPORT_FUNC (__aeabi_lmul) (grub_uint64_t a, grub_uint64_t b);

#endif

#if defined (__ia64__)

grub_uint64_t
EXPORT_FUNC (__udivdi3) (grub_uint64_t a, grub_uint64_t b);

grub_uint64_t
EXPORT_FUNC (__umoddi3) (grub_uint64_t a, grub_uint64_t b);

#endif

#ifdef __powerpc__

int
EXPORT_FUNC(__ucmpdi2) (grub_uint64_t a, grub_uint64_t b);

#endif

#if defined (__APPLE__) && defined(__i386__)
#define GRUB_BUILTIN_ATTR  __attribute__ ((regparm(0)))
#else
#define GRUB_BUILTIN_ATTR
#endif

/* Prototypes for aliases.  */
int GRUB_BUILTIN_ATTR EXPORT_FUNC(memcmp) (const void *s1, const void *s2, grub_size_t n);
void *GRUB_BUILTIN_ATTR EXPORT_FUNC(memmove) (void *dest, const void *src, grub_size_t n);
void *GRUB_BUILTIN_ATTR EXPORT_FUNC(memcpy) (void *dest, const void *src, grub_size_t n);
void *GRUB_BUILTIN_ATTR EXPORT_FUNC(memset) (void *s, int c, grub_size_t n);

#ifdef __APPLE__
void GRUB_BUILTIN_ATTR EXPORT_FUNC (__bzero) (void *s, grub_size_t n);
#endif

#endif

