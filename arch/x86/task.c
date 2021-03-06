#include <stdbool.h>
#include <string.h>
#include <task.h>
#include <mem.h>
#include <vga.h>
#include <helper.h>
#include <vm.h>
#include <gdt.h>
#include <cpio_parser.h>
#include <elf.h>
#include <syscall.h>
#include <sched.h>

static struct task *init_task;
/* PID of task, will be useful in fork */
static int pid;
/* Current task page directory */
static pd_t *current_pd;

extern void task_ret();

static int load_elf(struct task *t, pd_t *pd, void *data)
{
	int ret;
	Elf32_Ehdr *elf_hdr;

	elf_hdr = (Elf32_Ehdr *) data;
	if (strncmp((char *) elf_hdr->e_ident, ELFMAG, SELFMAG) != 0) {
		printk("Invalid ELF image\n");
		return -1;
	}
	if (elf_hdr->e_ident[EI_CLASS] != ELFCLASS32) {
		printk("Invalid ELF class, only 32 bit supported\n");
		return -1;
	}
	Elf32_Phdr *elf_sh = (Elf32_Phdr *) PTRINC(elf_hdr, elf_hdr->e_phoff);

	int i;
	for (i = 0; i < elf_hdr->e_phnum; i++) {
		if (elf_sh[i].p_type != PT_LOAD)
			continue;

		printk("Loading [virt: %p], [size: %x]\n",
				elf_sh[i].p_vaddr, elf_sh[i].p_memsz);
		ret = allocuvm(pd, (void *) elf_sh[i].p_vaddr,
						elf_sh[i].p_memsz);
		if (ret) {
			printk("allocuvm failure\n");
			return -1;
		}
		ret = loaduvm(pd, (void *) elf_sh[i].p_vaddr,
				PTRINC(elf_hdr, elf_sh[i].p_offset),
				elf_sh[i].p_filesz);
		if (ret) {
			printk("loaduvm failure\n");
			return -1;
		}
		/* Set system break */
		t->sbrk = elf_sh[i].p_vaddr + elf_sh[i].p_memsz;
	}

	/* Stack will be set up at last 4K in 4M region */
	ret = allocuvm(pd, (void *) (STACK_ADDR - STACK_SIZE), STACK_SIZE);
	if (ret) {
		printk("allocuvm failure\n");
		return -1;
	}
	/* FIXME: why stack needs to be little lower? */
	t->irqf->useresp = STACK_ADDR - 16;

	/* Set task entry point */
	t->irqf->eip = elf_hdr->e_entry;

	return 0;
}

struct task *alloctask()
{
	/* Allocate task control block */
	struct task *t = (struct task *) kcalloc(sizeof(struct task));
	if (!t) {
		printk("malloc failed\n");
		return NULL;
	}

	/* Allocate task stack, fixed to 4K for now */
	t->kstack = t->kstack_base =(uint8_t *) kcalloc_page(STACK_SIZE);
	if (!t->kstack) {
		printk("malloc failed\n");
		return NULL;
	}

	/* Create page tables, kernel code/heap is linked (not copied) */
	pd_t *new_pd = setupkvm();
	if (!new_pd) {
		printk("page dir clone failed\n");
		return NULL;
	}

	/* Setup stack frame as if task had been interrupted by exception,
         * with initial return point into `exit` routine of exception
         * handler.
         */
	t->id = ++pid;
	t->pd = new_pd;
	char *sp = (char *) ((uint32_t) t->kstack + STACK_SIZE);
	sp -= sizeof(*t->irqf);
	t->irqf = (registers_t *) sp;
	memset(t->irqf, 0, sizeof(*t->irqf));
	sp -= sizeof(*t->context);
	t->context = (struct context *) sp;
	memset(t->context, 0, sizeof(*t->context));
	t->context->eip = (uint32_t) task_ret;
	t->kstack += STACK_SIZE;

	/* Initialize wait queue with first task */
	list_add_tail(&t->next, task_list);

	return t;
}

extern char _binary_ramfs_cpio_start[];

int create_init_task()
{
	int ret;

	/* Initialize task list for scheduling */
	task_list = (list_head_t *) kmalloc(sizeof(list_head_t));
	if (!task_list) {
		printk("malloc failed\n");
		return -1;
	}
	INIT_LIST_HEAD(task_list);

	init_task = alloctask();
	if (!init_task) {
		printk("failed to create init task\n");
		return -1;
	}

	/* Task will start in CPL = 3, i.e. user mode */
	init_task->irqf->cs = (SEG_UCODE << 3) | DPL_USER;
	init_task->irqf->ds = (SEG_UDATA << 3) | DPL_USER;
	init_task->irqf->eflags = 0x200;
	init_task->irqf->ss = (SEG_UDATA << 3) | DPL_USER;

	unsigned long size;
	void *init = cpio_get_file(_binary_ramfs_cpio_start,
					INIT_TASK_NAME, &size);
	if (!init) {
		printk("no init task in initramfs\n");
		return -1;
	}

	ret = load_elf(init_task, init_task->pd, init);
	if (ret) {
		printk("failed to load elf\n");
		return -1;
	}

	init_task->state = TASK_READY;

	return 0;
}

void task_delete(struct task *new_task)
{
	list_del(&new_task->next);
	deallocvm(new_task->pd);
	kfree_page(new_task->kstack_base);
	kfree(new_task);
}

int create_idle_task()
{
	struct task *idle = alloctask();
	if (!idle) {
		printk("failed to create idle task\n");
		return -1;
	}

	/* Task will start in CPL = 0, i.e. kernel mode */
	idle->irqf->cs = (SEG_KCODE << 3);
	idle->irqf->ds = (SEG_KDATA << 3);
	idle->irqf->eflags = 0x200;
	idle->irqf->eip = (uint32_t) idle_loop;

	idle->state = TASK_READY;

	return 0;
}

int sys_fork()
{
	int ret;

	/* Allocate task control block */
	struct task *t = alloctask();
	if (!t) {
		printk("alloc task failed\n");
		return -1;
	}

	/* Clone page directory, kernel code/heap is linked (not copied),
	 * rest copied from parent process, i.e. curent process.
	 */
	ret = cloneuvm(t->pd, current_pd);
	if (ret) {
		printk("cloneuvm failed\n");
		return -1;
	}

	/* Clone exception frame from parent task */
	*t->irqf = *current_task->irqf;
	/* Clone parent sbrk limit */
	t->sbrk = current_task->sbrk;
	/* Child return value should be zero */
	t->irqf->eax = 0;

	/* Set parent task */
	t->parent = current_task;

	/* Set state for task */
	t->state = TASK_READY;

	return pid;
}

int sys_exec()
{
	char *fname;
	int ret;
	unsigned long size;

	argstr(0, &fname);

	void *fstart = cpio_get_file(_binary_ramfs_cpio_start,
					fname, &size);
	if (!fstart) {
		printk("no `%s` in ramfs\n", fname);
		return -1;
	}

	/* Create page tables, kernel code/heap is linked (not copied) */
	pd_t *new_pd = setupkvm();
	if (!new_pd) {
		printk("page dir clone failed\n");
		return -1;
	}

	ret = load_elf(current_task, new_pd, fstart);
	if (ret) {
		printk("failed to load elf\n");
		return -1;
	}

	/* Save old page dir pointer */
	pd_t *old_pd = current_task->pd;
	current_task->pd = new_pd;

	/* Save current pd to new pd */
	current_pd = new_pd;
	/* Switch to new page dir */
	switch_pgdir(V2P(current_task->pd));
	/* Now we can safely free older page dir */
	deallocvm(old_pd);

	return 0;
}

int sys_exit()
{
	/* Memory will be freed from IDLE task context */
	current_task->state = TASK_EXITED;
	task_wakeup(current_task->parent);
	sched();
	return 0;
}

int sys_waitpid()
{
	task_sleep(current_task);
	sched();
	return 0;
}

int sys_sbrk()
{
	int size;
	char *curr_limit;

	argint(0, &size);
	curr_limit = (char *) current_task->sbrk;

	if (size != 0) {
		if (((uintptr_t) curr_limit + size) > SBRK_LIMIT)
			return -1;
		allocuvm(current_pd, curr_limit, size);
		current_task->sbrk = (uintptr_t) curr_limit + size;
	}

	return (int) curr_limit;
}

void init_scheduler()
{
	if (!init_task) {
		printk("Scheduler invoked before creating task\n");
		return;
	}
	create_idle_task();
	current_task = init_task;
	set_kernel_stack((uint32_t) current_task->kstack);
	switch_pgdir(V2P(current_task->pd));
	current_pd = current_task->pd;
	current_task->state = TASK_RUNNING;
	load_context(current_task->context);
}

void tiny_schedule()
{
	struct task *_t = current_task;

	/* Globally disable interrupts, can be called from `yield` */
	cli();

	/* Find and set next task to schedule */
	next_to_schedule();

	/* See if context switch is required */
	if (_t != current_task) {
		/* Set kernel mode stack in task state segment */
		set_kernel_stack((uint32_t) current_task->kstack);

		/* Switch page directory to new task */
		switch_pgdir(V2P(current_task->pd));

		/* Set current page directory*/
		current_pd = current_task->pd;

		/* Switch context */
		swtch(&_t->context, current_task->context);
	}

	/* Globally enable interrupts */
	sti();
}

void yield()
{
	if (current_task && current_task->state == TASK_RUNNING) {
		current_task->state = TASK_READY;
		tiny_schedule();
	}
}

void sched()
{
	tiny_schedule();
}
