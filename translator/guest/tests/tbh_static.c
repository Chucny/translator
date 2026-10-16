#include <stdio.h>

// The table starts exactly at the architectural PC value of the TBH instruction. Entries 0-97
// select the near target; entry 98 selects the distant target. This catches a translator that
// branches to the table entry address/value instead of PC + 2 * ZeroExtend(table[index]).
__attribute__((aligned(4), naked, noinline, target("thumb"))) static int select_with_tbh(unsigned index) {
    __asm__ volatile(
        "tbh [pc, r0, lsl #1]\n"
        "0:\n"
        ".rept 98\n"
        ".short (1f - 0b) / 2\n"
        ".endr\n"
        ".short (2f - 0b) / 2\n"
        "1:\n"
        "movs r0, #17\n"
        "bx lr\n"
        "2:\n"
        "movs r0, #98\n"
        "bx lr\n");
}

int main(void) {
    const int near = select_with_tbh(0);
    const int distant = select_with_tbh(98);
    printf("tbh near=%d distant=%d\n", near, distant);
    return near == 17 && distant == 98 ? 0 : 1;
}
