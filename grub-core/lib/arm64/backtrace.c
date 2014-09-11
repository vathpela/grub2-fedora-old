/*
 *  GRUB  --  GRand Unified Bootloader
 *  Copyright (C) 2009  Free Software Foundation, Inc.
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

#include <grub/misc.h>
#include <grub/command.h>
#include <grub/err.h>
#include <grub/dl.h>
#include <grub/mm.h>
#include <grub/term.h>
#include <grub/backtrace.h>

#define MAX_STACK_FRAME 102400
#define ALIGN(addr, align) (addr + ((align - (addr % align)) % align))

static int
unwind_frame (unsigned long *fp, unsigned long *sp, unsigned long *pc)
{
  unsigned long high, low;
  unsigned long newfp = *fp;

  low = *sp;
  high = ALIGN(low, MAX_STACK_FRAME);

  if (newfp < low || newfp > high - 0x18 || newfp & 0xf)
    return -1;

  *sp = newfp + 0x10;
  *fp = *(unsigned long *)newfp;
  /* fp + 8 would be the pc when we return, but we want the caller instead */
  *pc = *(unsigned long *)(newfp + 8) - 4;

  return 0;
}

void
grub_backtrace (void)
{
  register unsigned long current_sp asm("sp");
  unsigned long fp = (unsigned long)__builtin_frame_address(0);
  unsigned long pc = (unsigned long)grub_backtrace;
  unsigned long sp = current_sp;

  while (1)
    {
      grub_printf ("sp: %p fp: %p pc: ", (void *)sp, (void *)fp);
      grub_backtrace_print_address ((void *)pc);
      grub_printf ("\n");

      if (unwind_frame (&fp, &sp, &pc) < 0)
	break;
    }
}
