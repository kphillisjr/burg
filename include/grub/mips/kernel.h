/*
 *  GRUB  --  GRand Unified Bootloader
 *  Copyright (C) 2005,2006,2007,2008  Free Software Foundation, Inc.
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

#ifndef GRUB_KERNEL_CPU_HEADER
#define GRUB_KERNEL_CPU_HEADER	1

#define GRUB_MOD_ALIGN 0x1
/* Non-zero value is only needed for PowerMacs.  */
#define GRUB_MOD_GAP   0x0

#ifndef ASM_FILE

/* The prefix which points to the directory where GRUB modules and its
   configuration file are located.  */
extern char grub_prefix[];

#endif

#endif
