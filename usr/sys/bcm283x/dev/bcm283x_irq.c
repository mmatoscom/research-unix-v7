#include "bcm283x_irq.h"

#include "bcm283x_io.h"

#include "../trap.h"
#include "../kstddef.h"
#include "../kstdint.h"
#include <errno.h>


#define INTC_OFFSET                     0x0000b000
#define INTC_BASE                       ((_bcm283x_iobase) + (INTC_OFFSET))

#define ALT_OFFSET                      0x01000000
#define ALT_BASE                        ((_bcm283x_iobase) + (ALT_OFFSET))

#define ALT_CORE_IRQ_SOURCE_OFFSET_N(N) (0x60 + (0x04 * (N)))
#define ALT_CORE_IRQ_SOURCE_REG_N(N)    ((ALT_BASE) + ALT_CORE_IRQ_SOURCE_OFFSET_N((N)))

#define IRQ_BASIC_PENDING_REG_OFFSET    0x00000200
#define IRQ_PENDING_1_REG_OFFSET        0x00000204
#define IRQ_PENDING_2_REG_OFFSET        0x00000208
#define FIQ_CONTROL_REG_OFFSET          0x0000020c
#define ENABLE_IRQS_1_REG_OFFSET        0x00000210
#define ENABLE_IRQS_2_REG_OFFSET        0x00000214
#define ENABLE_BASIC_IRQS_REG_OFFSET    0x00000218
#define DISABLE_IRQS_1_REG_OFFSET       0x0000021c
#define DISABLE_IRQS_2_REG_OFFSET       0x00000220
#define DISABLE_BASIC_IRQS_REG_OFFSET   0x00000224

#define IRQ_BASIC_PENDING_REG           ((INTC_BASE) + (IRQ_BASIC_PENDING_REG_OFFSET))
#define IRQ_PENDING_1_REG               ((INTC_BASE) + (IRQ_PENDING_1_REG_OFFSET))
#define IRQ_PENDING_2_REG               ((INTC_BASE) + (IRQ_PENDING_2_REG_OFFSET))
#define FIQ_CONTROL_REG                 ((INTC_BASE) + (FIQ_CONTROL_REG_OFFSET))
#define ENABLE_IRQS_1_REG               ((INTC_BASE) + (ENABLE_IRQS_1_REG_OFFSET))
#define ENABLE_IRQS_2_REG               ((INTC_BASE) + (ENABLE_IRQS_2_REG_OFFSET))
#define ENABLE_BASIC_IRQS_REG           ((INTC_BASE) + (ENABLE_BASIC_IRQS_REG_OFFSET))
#define DISABLE_IRQS_1_REG              ((INTC_BASE) + (DISABLE_IRQS_1_REG_OFFSET))
#define DISABLE_IRQS_2_REG              ((INTC_BASE) + (DISABLE_IRQS_2_REG_OFFSET))
#define DISABLE_BASIC_IRQS_REG          ((INTC_BASE) + (DISABLE_BASIC_IRQS_REG_OFFSET))

#define PENDING_1_NON_REFLECTED_BITS    (~(BIT( 7) | \
                                           BIT( 9) | \
                                           BIT(10) | \
                                           BIT(18) | \
                                           BIT(19)))
#define PENDING_2_NON_REFLECTED_BITS    (~(BIT(21) | \
                                           BIT(22) | \
                                           BIT(23) | \
                                           BIT(24) | \
                                           BIT(25) | \
                                           BIT(30)))
#define BASIC_PENDING_INTERESTING_BITS  0x001fffff
#define NO_BASIC_PENDING_OTHER_BITS     (~(BIT( 8) | \
                                           BIT( 9)))

#define NUM_IRQ_HANDLERS (CORE_IRQ_MAX + 1)
static irq_handler_t irq_handlers[NUM_IRQ_HANDLERS];

extern devaddr_t _bcm283x_iobase;          /* peripheral base address */
extern uint32_t _bcm283x_has_core_block;
extern void panic(const char *s) __attribute__((noreturn));
extern uint32_t current_core(void);


static int enable_irq_line(uint32_t irqnum)
{
  if (irqnum < 32) {
    iowrite32(ENABLE_IRQS_1_REG, BIT(irqnum));
  } else if (irqnum < 64) {
    iowrite32(ENABLE_IRQS_2_REG, BIT(irqnum - 32));
  } else if (irqnum < 72) {
    iowrite32(ENABLE_BASIC_IRQS_REG, BIT(irqnum - 64));
  } else if (irqnum < NUM_IRQ_HANDLERS) {
    /* FIXME */
  } else {
    return -EINVAL;
  }

  return 0;
}


static int disable_irq_line(uint32_t irqnum)
{
  if (irqnum < 32) {
    iowrite32(DISABLE_IRQS_1_REG, BIT(irqnum));
  } else if (irqnum < 64) {
    iowrite32(DISABLE_IRQS_2_REG, BIT(irqnum - 32));
  } else if (irqnum < 72) {
    iowrite32(DISABLE_BASIC_IRQS_REG, BIT(irqnum - 64));
  } else if (irqnum < NUM_IRQ_HANDLERS) {
    /* FIXME */
  } else {
    return -EINVAL;
  }

  return 0;
}


int bcm283x_register_irq_handler(uint32_t irqnum, void (*fn)(void*), void *arg)
{
  irq_handler_t *handler;

  if (irqnum >= NUM_IRQ_HANDLERS || !fn) {
    return -EINVAL;
  }

  handler = &irq_handlers[irqnum];

  if (handler->fn || handler->tfn) {
    return -EBUSY;
  }

  handler->fn = fn;
  handler->arg = arg;

  if (enable_irq_line(irqnum) != 0) {
    handler->fn = NULL;
    handler->arg = NULL;
    return -EINVAL;
  }

  return 0;
}


int bcm283x_register_timer_irq_handler(uint32_t irqnum, void (*fn)(void*, struct tf_regs_t *), void *arg)
{
  irq_handler_t *handler;

  if (irqnum >= NUM_IRQ_HANDLERS || !fn) {
    return -EINVAL;
  }

  handler = &irq_handlers[irqnum];

  if (handler->fn || handler->tfn) {
    return -EBUSY;
  }

  handler->tfn = fn;
  handler->arg = arg;

  if (enable_irq_line(irqnum) != 0) {
    handler->fn = NULL;
    handler->tfn = NULL;
    handler->arg = NULL;
    return -EINVAL;
  }

  return 0;
}


int bcm283x_deregister_irq_handler(uint32_t irqnum)
{
  irq_handler_t *handler;

  if (irqnum >= NUM_IRQ_HANDLERS) {
    return -EINVAL;
  }

  if (disable_irq_line(irqnum) != 0) {
    return -EINVAL;
  }

  handler = &irq_handlers[irqnum];

  handler->fn = NULL;
  handler->tfn = NULL;
  handler->arg = NULL;

  return 0;
}


static void do_process_irq(uint32_t irqnum, struct tf_regs_t *tf)
{
  irq_handler_t *handler;

  if (irqnum >= NUM_IRQ_HANDLERS) {
    panic("do_process_irq: bad interrupt number");
    return;
  }

  handler = &irq_handlers[irqnum];

  if (!handler->fn && !handler->tfn) {
    panic("do_process_irq: spurious interrupt");
    return;
  }

  if (handler->tfn) {
    handler->tfn(handler->arg, tf);
  } else {
    handler->fn(handler->arg);
  }
}


/*
 * C-level IRQ handler called from the ASM trap handler.
 */
void irqc(struct tf_regs_t *tf)
{
  uint32_t core;
  uint32_t core_src;
  uint32_t pend0;
  uint32_t pend1;
  uint32_t pend2;
  uint32_t i;

  if (_bcm283x_has_core_block) {
    core = current_core();
    core_src = ioread32(ALT_CORE_IRQ_SOURCE_REG_N(core)) & 0xfff;
  }

  pend0 = ioread32(IRQ_BASIC_PENDING_REG) & BASIC_PENDING_INTERESTING_BITS;

  if (pend0 & BIT(8)) {
    pend1 = ioread32(IRQ_PENDING_1_REG) & PENDING_1_NON_REFLECTED_BITS;
  } else {
    pend1 = 0;
  }

  if (pend0 & BIT(9)) {
    pend2 = ioread32(IRQ_PENDING_2_REG) & PENDING_2_NON_REFLECTED_BITS;
  } else {
    pend2 = 0;
  }

  pend0 &= NO_BASIC_PENDING_OTHER_BITS;

  /*
   * TODO: Perhaps mask off higher pri stuff and handle it first?
   */

  if (_bcm283x_has_core_block && core_src) {
    for (i = 0; i < 12 && core_src; ++i, core_src >>= 1) {
      if (core_src & 1) {
        do_process_irq(CORE_IRQ_BASE + (12 * core) + i, tf);
      }
    }
  }

  if (pend0) {
    /*
     * Bits 7:0 are ARM IRQs, and map to logical IRQs 71..64.
     */
    for (i = 0; i < 8; ++i) {
      if (pend0 & BIT(i)) {
        do_process_irq(64 + i, tf);
      }
    }

    /*
     * Bit 10 maps to GPU IRQ 7.
     */
    if (pend0 & BIT(10)) {
      do_process_irq(7, tf);
    }

    /*
     * Bits 12..11 map to GPU IRQs 10..9.
     */
    for (i = 11; i < 13; ++i) {
      if (pend0 & BIT(i)) {
        do_process_irq(i - 2, tf);
      }
    }

    /*
     * Bits 14..13 map to GPU IRQs 19..18.
     */
    for (i = 13; i < 15; ++i) {
      if (pend0 & BIT(i)) {
        do_process_irq(i + 5, tf);
      }
    }

    /*
     * Bits 19..15 map to GPU IRQs 57..53.
     */
    for (i = 15; i < 20; ++i) {
      if (pend0 & BIT(i)) {
        do_process_irq(i + 38, tf);
      }
    }

    /*
     * Bit 20 maps to GPU IRQ 62.
     */
    if (pend0 & BIT(20)) {
      do_process_irq(62, tf);
    }
  }

  /*
   * Now we dispatch 'GPU' IRQs that are not mapped to 'basic pending'
   * shortcut bits.
   */

  if (pend1) {
    for (i = 0; i < 32; ++i) {
      if (pend1 & BIT(i)) {
        do_process_irq(i, tf);
      }
    }
  }

  if (pend2) {
    for (i = 0; i < 32; ++i) {
      if (pend2 & BIT(i)) {
        do_process_irq(32 + i, tf);
      }
    }
  }
}


void bcm283x_init_irq()
{
  uint32_t i;
  irq_handler_t *handler;

  iowrite32(DISABLE_IRQS_1_REG, ~0U);
  iowrite32(DISABLE_IRQS_2_REG, ~0U);
  iowrite32(DISABLE_BASIC_IRQS_REG, ~0U);
  iowrite32(FIQ_CONTROL_REG, 0U);
  /* FIXME */

  for (i = 0; i <= ARM_IRQ_MAX; ++i) {
    handler = &irq_handlers[i];
    handler->fn = NULL;
    handler->arg = NULL;
  }
}


void bcm283x_deinit_irq()
{
  bcm283x_init_irq();  /* disables all IRQs unconditionally */
}
