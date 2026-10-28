.syntax unified
.arm
.global _start
.type _start,%function
_start:
 mov r0,sp
 bic sp,sp,#7
 bl start_c
 b .
