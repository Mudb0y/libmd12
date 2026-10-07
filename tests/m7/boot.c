/* Runs the test suite on QEMU's Cortex-M7: turns the floating point unit on,
   clears .bss, opens the semihosting console and calls it. */
#include <stdint.h>
#include <string.h>

int test_main(void);
void initialise_monitor_handles(void);
void exit(int);

extern char __bss_start__[], __bss_end__[], __stack_top[];

__attribute__((naked, noreturn)) void m7_reset(void)
{
    __asm__ volatile(
        "ldr r0, =0xE000ED88\n"
        "ldr r1, [r0]\n"
        "orr r1, r1, #(0xF << 20)\n"
        "str r1, [r0]\n"
        "dsb\n"
        "isb\n"
        "bl  m7_main\n"
        "b   .\n"
        ".ltorg\n");
}

__attribute__((used)) void m7_main(void)
{
    memset(__bss_start__, 0, (size_t)(__bss_end__ - __bss_start__));
    initialise_monitor_handles();
    exit(test_main());
}

static void fault(void)
{
    exit(4);
}

__attribute__((section(".vectors"), used))
static const uintptr_t vectors[16] = {
    (uintptr_t)__stack_top,
    (uintptr_t)m7_reset,
    (uintptr_t)fault, (uintptr_t)fault, (uintptr_t)fault,
    (uintptr_t)fault, (uintptr_t)fault,
};

/* Without the C runtime's start files, newlib still calls these. */
void _init(void) {}
void _fini(void) {}
