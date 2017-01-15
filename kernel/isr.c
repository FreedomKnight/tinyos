#include <isr.h>
#include <vga.h>
#include <helper.h>

static void *irq_handlers[16];

/* This gets called from our ASM interrupt handler stub */
void isr_handler(registers_t *regs)
{
	printk("recieved interrupt: %d\n", regs->int_no);
	if (regs->int_no < 0x20) {
		printk("system generated exception, HALT!\n");
		while(1);
	}
}

/* This installs a custom IRQ handler for the given IRQ */
void irq_install_handler(int irq, void (*handler)(registers_t *r))
{
	irq_handlers[irq] = handler;
}

/* This clears the handler for a given IRQ */
void irq_uninstall_handler(int irq)
{
	irq_handlers[irq] = 0;
}

// This gets called from our ASM interrupt handler stub.
void irq_handler(registers_t *r)
{
	/* This is a blank function pointer */
	void (*handler)(registers_t *r);

	/* Find out if we have a custom handler to run for this
	 *  IRQ, and then finally, run it */
	handler = irq_handlers[r->int_no - 32];
	if (handler)
		handler(r);

	/* If the IDT entry that was invoked was greater than 40
	 *  (meaning IRQ8 - 15), then we need to send an EOI to
	 *  the slave controller */
	if (r->int_no >= 40)
		outportb(0xA0, 0x20);

	/* In either case, we need to send an EOI to the master
	 *  interrupt controller too */
	outportb(0x20, 0x20);
}
