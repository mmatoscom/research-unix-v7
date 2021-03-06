#include "bcm283x_mbox.h"

#include "bcm283x_io.h"
#include "../arm1176jzfs.h"
#include "../bcm283x_machdep.h"
#include "bcm283x_pl011.h"

#include "../kstddef.h"

#define MBOX_OFFSET                   0x0000b880
#define MBOX_BASE                     ((_bcm283x_iobase) + (MBOX_OFFSET))

#define MBOX0_RW_REG_OFFSET           0x00000000
#define MBOX0_PEEK_REG_OFFSET         0x00000010
#define MBOX0_SENDER_REG_OFFSET       0x00000014
#define MBOX0_STATUS_REG_OFFSET       0x00000018
#define MBOX0_CONFIG_REG_OFFSET       0x0000001c
#define MBOX1_RW_REG_OFFSET           0x00000020
#define MBOX1_PEEK_REG_OFFSET         0x00000030
#define MBOX1_SENDER_REG_OFFSET       0x00000034
#define MBOX1_STATUS_REG_OFFSET       0x00000038
#define MBOX1_CONFIG_REG_OFFSET       0x0000003c

#define MBOX0_RW_REG                  ((MBOX_BASE) + (MBOX0_RW_REG_OFFSET))
#define MBOX0_PEEK_REG                ((MBOX_BASE) + (MBOX0_PEEK_REG_OFFSET))
#define MBOX0_SENDER_REG              ((MBOX_BASE) + (MBOX0_SENDER_REG_OFFSET))
#define MBOX0_STATUS_REG              ((MBOX_BASE) + (MBOX0_STATUS_REG_OFFSET))
#define MBOX0_CONFIG_REG              ((MBOX_BASE) + (MBOX0_CONFIG_REG_OFFSET))
#define MBOX1_RW_REG                  ((MBOX_BASE) + (MBOX1_RW_REG_OFFSET))
#define MBOX1_PEEK_REG                ((MBOX_BASE) + (MBOX1_PEEK_REG_OFFSET))
#define MBOX1_SENDER_REG              ((MBOX_BASE) + (MBOX1_SENDER_REG_OFFSET))
#define MBOX1_STATUS_REG              ((MBOX_BASE) + (MBOX1_STATUS_REG_OFFSET))
#define MBOX1_CONFIG_REG              ((MBOX_BASE) + (MBOX1_CONFIG_REG_OFFSET))

#define MBOX_STATUS_FULL_BIT          31
#define MBOX_STATUS_EMPTY_BIT         30

#define MBOX_STATUS_FULL_MASK         BIT(MBOX_STATUS_FULL_BIT)
#define MBOX_STATUS_EMPTY_MASK        BIT(MBOX_STATUS_EMPTY_BIT)

#define MBOX_PROP_CHAN_ARM_TO_VC      8
#define MBOX_PROP_CHAN_VC_TO_ARM      9

static volatile uint32_t  __attribute__((aligned(16))) mbox_buffer[36];

extern devaddr_t _bcm283x_iobase;          /* peripheral base address */
extern devaddr_t _bcm283x_p2v_offset;      /* physical space to kernel space offset */

static int dump_debug = 0;

/*
 * ```The ARM should never write MB 0 or read MB 1.```
 * ergo, read from MB0, write to MB1
 * See https://github.com/raspberrypi/firmware/wiki/Mailboxes
 */

static uint32_t bcm283x_mbox_read(uint8_t chan)
{
  uint32_t v;

  while (1) {
    while (ioread32(MBOX0_STATUS_REG) & MBOX_STATUS_EMPTY_MASK);
    v = ioread32(MBOX0_RW_REG);
    if ((v & 0xf) == chan) {
      break;
    }
  }

  return v & 0xfffffff0;
}


static void bcm283x_mbox_write(uint8_t chan, uint32_t v)
{
  while (ioread32(MBOX1_STATUS_REG) & MBOX_STATUS_FULL_MASK);
  iowrite32(MBOX1_RW_REG, (v & 0xfffffff0) | (chan & 0xf));
}

/*
 * mbox_buffer[0]: buffer size in bytes
 * mbox_buffer[1]: request/response code
 *                  REQ:
 *                   Process Request: 0x00000000
 *                  RSP:
 *                   Request Success: 0x80000000
 *                   Parse Error    : 0x80000001
 * mbox_buffer[2..]: concatenated tags
 * mbox_buffer[-1]: End tag: 0x0
 * mbox_buffer[..]: Padding (0x0)
 *
 * Tag Format
 * ==========
 *
 * u32: tag identifier
 * u32: value buffer size in bytes
 * u32:
 *   Request codes:
 *     b31 clear: request
 *     b30-b0: reserved
 *   Response codes:
 *     b31 set: response
 *     b30-b0: value length in bytes
 * u8...: value buffer
 * u8...: padding to align the tag to 32 bits.
 */

#define MBOX_REQUEST_PROCESS          0x00000000
#define MBOX_RESPONSE_SUCCESS         0x80000000
#define MBOX_RESONSE_PARSE_ERROR      0x80000001

#define MBOX_TAG_REQUEST_CODE         0x00000000
#define MBOX_TAG_RESPONSE_CODE        0x80000000
#define MBOX_TAG_GETSERIAL            0x00010004
#define MBOX_TAG_ARM_MEMORY           0x00010005
#define MBOX_TAG_GETCLKRATE           0x00030002
#define MBOX_TAG_SETCLKRATE           0x00038002
#define MBOX_TAG_END                  0x00000000

static int bcm283x_mbox_write_then_read(uint8_t chan)
{
  uint32_t v;

  if (dump_debug) {
    uint32_t i;
    printf("BEFORE:\n");
    for (i = 0; i < 13; ++i) {
      printf("  mbox_buffer[%u]: 0x%08x\n", i, mbox_buffer[i]);
    }
  }

  DMB;
  do_clean_and_invalidate_dcache();
  DSB; ISB;

  bcm283x_mbox_write(chan, MEM_VIRT_TO_PHYS(&mbox_buffer[0]));
  do_invalidate_dcache();
  v = bcm283x_mbox_read(chan);
  ISB;

  if (dump_debug) {
    uint32_t i;
    printf("AFTER:\n");
    for (i = 0; i < 13; ++i) {
      printf("  mbox_buffer[%u]: 0x%08x\n", i, mbox_buffer[i]);
    }
  }

  if (v != MEM_VIRT_TO_PHYS(&mbox_buffer[0])) {
    return -1;  /* -EBADMSG */
  }

  if (mbox_buffer[1] != MBOX_RESPONSE_SUCCESS) {
    return -1;  /* -EBADMSG */
  }

  return 0;
}


int bcm283x_mbox_get_arm_memory(uint32_t *v)
{
  /* dump_debug = 1; */

  mbox_buffer[0] = 8 * sizeof(mbox_buffer[0]);
  mbox_buffer[1] = MBOX_REQUEST_PROCESS;
  mbox_buffer[2] = MBOX_TAG_ARM_MEMORY;      /* tag identifier */
  mbox_buffer[3] = 8;                        /* value buffer size in bytes */
  mbox_buffer[4] = 0;                        /* request code: b31 clear, request */
  mbox_buffer[5] = MBOX_TAG_END;             /* value buffer */
  mbox_buffer[6] = 0x00;                     /* initialise return area */
  mbox_buffer[7] = 0x00;                     /* initialise return area */

  if (0 != bcm283x_mbox_write_then_read(MBOX_PROP_CHAN_ARM_TO_VC)) {
    return -1;
  }

  *v = mbox_buffer[6];
  return 0;
}


int bcm283x_mbox_set_uart_clock(uint32_t hz, uint32_t *new_hz)
{
  mbox_buffer[0] = 9 * sizeof(mbox_buffer[0]);
  mbox_buffer[1] = MBOX_REQUEST_PROCESS;
  mbox_buffer[2] = MBOX_TAG_SETCLKRATE;     /* tag identifier */
  mbox_buffer[3] = 3 * 4;                   /* value buffer size in bytes */
  mbox_buffer[4] = 8;                       /* request code: b31 clear, request - WTF is this? */
  mbox_buffer[5] = 2;                       /* clock id: UART clock */
  mbox_buffer[6] = hz;                      /* value buffer */
  mbox_buffer[7] = 0;                       /* clear turbo */
  mbox_buffer[8] = MBOX_TAG_END;            /* end tag */

  if (0 != bcm283x_mbox_write_then_read(MBOX_PROP_CHAN_ARM_TO_VC)) {
    return -1;
  }

  if (new_hz) {
    *new_hz = mbox_buffer[6];
  }

  return 0;
}


int bcm283x_mbox_get_sdcard_clock(uint32_t *hz)
{
  /* dump_debug = 1; */

  mbox_buffer[0] = 13 * sizeof(mbox_buffer[0]);
  mbox_buffer[1] = MBOX_REQUEST_PROCESS;
  mbox_buffer[2] = MBOX_TAG_GETCLKRATE;     /* tag identifier */
  mbox_buffer[3] = 1 * 4;                   /* value buffer size in bytes */
  mbox_buffer[4] = 4;                       /* request code: b31 clear, request - WTF is this? */
  mbox_buffer[5] = 0x000000001;             /* clock id: EMMC clock */
  mbox_buffer[6] = MBOX_TAG_END;            /* end tag */
  mbox_buffer[7] = 0x0;                     /* response space */

  if (0 != bcm283x_mbox_write_then_read(MBOX_PROP_CHAN_ARM_TO_VC)) {
    return -1;
  }

  if (hz) {
    *hz = mbox_buffer[6];
  }

  return 0;
}
