/* Success-only network command stub for the disposable private netd probe.
 * It never programs routes or firewall rules. The launcher reports once that
 * these explicit test shims are installed, avoiding a flood of log messages. */
__attribute__((naked, noreturn)) void _start(void) {
    __asm__ volatile(
        "mov r0, #0\n"
        "mov r7, #1\n"
        "svc #0\n");
}
