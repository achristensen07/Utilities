; declare external functions if you want to call them
EXTERN sin : far

PUBLIC assembly

_TEXT SEGMENT

assembly PROC

ret
; put 64-bit assembly here, put a break point on ret, and open the disassembly window to see the hex values of different assembly instructions

assembly ENDP

_TEXT ENDS

END