/* pxe.c - Driver to provide access to the pxe filesystem  */
/*
 *  GRUB  --  GRand Unified Bootloader
 *  Copyright (C) 2008,2009,2011  Free Software Foundation, Inc.
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

#include <grub/dl.h>
#include <grub/net.h>
#include <grub/mm.h>
#include <grub/file.h>
#include <grub/misc.h>
#include <grub/bufio.h>
#include <grub/env.h>

#include <grub/machine/pxe.h>
#include <grub/machine/int.h>
#include <grub/machine/memory.h>

GRUB_MOD_LICENSE ("GPLv3+");

#define SEGMENT(x)	((x) >> 4)
#define OFFSET(x)	((x) & 0xF)
#define SEGOFS(x)	((SEGMENT(x) << 16) + OFFSET(x))
#define LINEAR(x)	(void *) ((((x) >> 16) << 4) + ((x) & 0xFFFF))

struct grub_pxe_undi_open
{
  grub_uint16_t status;
  grub_uint16_t open_flag;
  grub_uint16_t pkt_filter;
  grub_uint16_t mcast_count;
  grub_uint8_t mcast[8][6];
} __attribute__ ((packed));

struct grub_pxe_undi_isr
{
  grub_uint16_t status;
  grub_uint16_t func_flag;
  grub_uint16_t buffer_len;
  grub_uint16_t frame_len;
  grub_uint16_t frame_hdr_len;
  grub_uint32_t buffer;
  grub_uint8_t prot_type;
  grub_uint8_t pkt_type;
} __attribute__ ((packed));

enum
  {
    GRUB_PXE_ISR_IN_START = 1,
    GRUB_PXE_ISR_IN_PROCESS,
    GRUB_PXE_ISR_IN_GET_NEXT
  };

enum
  {
    GRUB_PXE_ISR_OUT_OURS = 0,
    GRUB_PXE_ISR_OUT_NOT_OURS = 1
  };

enum
  {
    GRUB_PXE_ISR_OUT_DONE = 0,
    GRUB_PXE_ISR_OUT_TRANSMIT = 2,
    GRUB_PXE_ISR_OUT_RECEIVE = 3,
    GRUB_PXE_ISR_OUT_BUSY = 4,
  };

struct grub_pxe_undi_transmit
{
  grub_uint16_t status;
  grub_uint8_t protocol;
  grub_uint8_t xmitflag;
  grub_uint32_t dest;
  grub_uint32_t tbd;
  grub_uint32_t reserved[2];
} __attribute__ ((packed));

struct grub_pxe_undi_tbd
{
  grub_uint16_t len;
  grub_uint32_t buf;
  grub_uint16_t blk_count;
  struct
  {
    grub_uint8_t ptr_type;
    grub_uint8_t reserved;
    grub_uint16_t len;
    grub_uint32_t ptr;
  } blocks[8];
} __attribute__ ((packed));

struct grub_pxe_bangpxe *grub_pxe_pxenv;
static grub_uint32_t pxe_rm_entry = 0;

static struct grub_pxe_bangpxe *
grub_pxe_scan (void)
{
  struct grub_bios_int_registers regs;
  struct grub_pxenv *pxenv;
  struct grub_pxe_bangpxe *bangpxe;

  regs.flags = GRUB_CPU_INT_FLAGS_DEFAULT;

  regs.ebx = 0;
  regs.ecx = 0;
  regs.eax = 0x5650;
  regs.es = 0;

  grub_bios_interrupt (0x1a, &regs);

  if ((regs.eax & 0xffff) != 0x564e)
    return NULL;

  pxenv = (struct grub_pxenv *) ((regs.es << 4) + (regs.ebx & 0xffff));
  if (grub_memcmp (pxenv->signature, GRUB_PXE_SIGNATURE,
		   sizeof (pxenv->signature))
      != 0)
    return NULL;

  if (pxenv->version < 0x201)
    return NULL;

  bangpxe = (void *) ((((pxenv->pxe_ptr & 0xffff0000) >> 16) << 4)
		      + (pxenv->pxe_ptr & 0xffff));

  if (!bangpxe)
    return NULL;

  if (grub_memcmp (bangpxe->signature, GRUB_PXE_BANGPXE_SIGNATURE,
		   sizeof (bangpxe->signature)) != 0)
    return NULL;

  pxe_rm_entry = bangpxe->rm_entry;

  return bangpxe;
}

static grub_ssize_t 
grub_pxe_recv (const struct grub_net_card *dev __attribute__ ((unused)),
	       struct grub_net_buff *buf)
{
  struct grub_pxe_undi_isr *isr;
  static int in_progress = 0;
  char *ptr, *end;
  int len;

  isr = (void *) GRUB_MEMORY_MACHINE_SCRATCH_ADDR;

  if (!in_progress)
    {
      grub_memset (isr, 0, sizeof (*isr));
      isr->func_flag = GRUB_PXE_ISR_IN_START;
      grub_pxe_call (GRUB_PXENV_UNDI_ISR, isr, pxe_rm_entry);
      if (isr->status || isr->func_flag != GRUB_PXE_ISR_OUT_OURS)
	return -1;
      grub_memset (isr, 0, sizeof (*isr));
      isr->func_flag = GRUB_PXE_ISR_IN_PROCESS;
      grub_pxe_call (GRUB_PXENV_UNDI_ISR, isr, pxe_rm_entry);
    }
  else
    {
      grub_memset (isr, 0, sizeof (*isr));
      isr->func_flag = GRUB_PXE_ISR_IN_GET_NEXT;
      grub_pxe_call (GRUB_PXENV_UNDI_ISR, isr, pxe_rm_entry);
    }

  while (isr->func_flag != GRUB_PXE_ISR_OUT_RECEIVE)
    {
      if (isr->status || isr->func_flag == GRUB_PXE_ISR_OUT_DONE)
	return -1;
      grub_memset (isr, 0, sizeof (*isr));
      isr->func_flag = GRUB_PXE_ISR_IN_GET_NEXT;
      grub_pxe_call (GRUB_PXENV_UNDI_ISR, isr, pxe_rm_entry);
    }

  grub_netbuff_put (buf, isr->frame_len);
  ptr = buf->data;
  end = ptr + isr->frame_len;
  len = isr->frame_len;
  grub_memcpy (ptr, LINEAR (isr->buffer), isr->buffer_len);
  ptr += isr->buffer_len;
  while (ptr < end)
    {
      grub_memset (isr, 0, sizeof (*isr));
      isr->func_flag = GRUB_PXE_ISR_IN_GET_NEXT;
      grub_pxe_call (GRUB_PXENV_UNDI_ISR, isr, pxe_rm_entry);
      if (isr->status || isr->func_flag != GRUB_PXE_ISR_OUT_RECEIVE)
	return -1;

      grub_memcpy (ptr, LINEAR (isr->buffer), isr->buffer_len);
      ptr += isr->buffer_len;
    }

  grub_printf ("<%d>\n", len);
  return len;
}

static grub_err_t 
grub_pxe_send (const struct grub_net_card *dev __attribute__ ((unused)),
	       struct grub_net_buff *pack)
{
  struct grub_pxe_undi_transmit *trans;
  struct grub_pxe_undi_tbd *tbd;
  char *buf;

  trans = (void *) GRUB_MEMORY_MACHINE_SCRATCH_ADDR;
  grub_memset (trans, 0, sizeof (*trans));
  tbd = (void *) (GRUB_MEMORY_MACHINE_SCRATCH_ADDR + 128);
  grub_memset (tbd, 0, sizeof (*tbd));
  buf = (void *) (GRUB_MEMORY_MACHINE_SCRATCH_ADDR + 256);
  grub_memcpy (buf, pack->data, pack->tail - pack->data);

  trans->tbd = SEGOFS ((grub_addr_t) tbd);
  trans->protocol = 0;
  tbd->len = pack->tail - pack->data;
  tbd->buf = SEGOFS ((grub_addr_t) buf);

  grub_pxe_call (GRUB_PXENV_UNDI_TRANSMIT, trans, pxe_rm_entry);
  if (trans->status)
    return grub_error (GRUB_ERR_IO, "PXE send failed (status 0x%x)",
		       trans->status);
  return 0;
}

struct grub_net_card_driver grub_pxe_card_driver =
{
  .send = grub_pxe_send,
  .recv = grub_pxe_recv
};

struct grub_net_card grub_pxe_card =
{
  .driver = &grub_pxe_card_driver,
  .name = "pxe"
};

void
grub_pxe_unload (void)
{
  if (grub_pxe_pxenv)
    {
      grub_pxe_call (GRUB_PXENV_UNDI_CLOSE,
		     (void *) GRUB_MEMORY_MACHINE_SCRATCH_ADDR,
		     pxe_rm_entry);
      grub_net_card_unregister (&grub_pxe_card);
      grub_pxe_pxenv = 0;
    }
}

GRUB_MOD_INIT(pxe)
{
  struct grub_pxe_bangpxe *pxenv;
  struct grub_pxenv_get_cached_info ci;
  struct grub_net_bootp_packet *bp;
  struct grub_pxe_undi_open *ou;

  pxenv = grub_pxe_scan ();
  if (! pxenv)
    return;

  ci.packet_type = GRUB_PXENV_PACKET_TYPE_DHCP_ACK;
  ci.buffer = 0;
  ci.buffer_size = 0;
  grub_pxe_call (GRUB_PXENV_GET_CACHED_INFO, &ci, pxe_rm_entry);
  if (ci.status)
    return;

  bp = LINEAR (ci.buffer);

  grub_memcpy (grub_pxe_card.default_address.mac, bp->mac_addr,
	       bp->hw_len < sizeof (grub_pxe_card.default_address.mac)
	       ? bp->hw_len : sizeof (grub_pxe_card.default_address.mac));
  grub_pxe_card.default_address.type = GRUB_NET_LINK_LEVEL_PROTOCOL_ETHERNET;

  ou = (void *) GRUB_MEMORY_MACHINE_SCRATCH_ADDR;
  grub_memset (ou, 0, sizeof (ou));
  ou->pkt_filter = 4;
  grub_pxe_call (GRUB_PXENV_UNDI_OPEN, ou, pxe_rm_entry);
  
  if (ou->status)
    return;

  grub_net_card_register (&grub_pxe_card);
  grub_net_configure_by_dhcp_ack ("pxe", &grub_pxe_card, 0,
				  bp, GRUB_PXE_BOOTP_SIZE);
}

GRUB_MOD_FINI(pxe)
{
  grub_pxe_unload ();
}
