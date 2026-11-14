## 12C address
    0x68(7bit)
    8bit address write: 0xD0
    8bit address read:  0xD1

## Back mode (normal operation)
    read register
    step 1: command:            F2 AA 55 0F F0 68
    step 2: read instruction:   F3 <address>

    write register
    step 1: command:            F2 AA 55 0F F0 68
    step 2: write instruction:  F2 <address>

## Non backdoor mode (not normally used)
    read register
    ???

## Touch panel interrupt usage
    Interrupt triggers on falling edge, and is auto cleared, while touch is detected, interrupt is continuously re-triggered.

## Other address
    Chip ID:    0x40008076 (read only, return 0x9032, can be used to check for for liveliness)
    Soft reset: 0x40008004 (write with value 0xA5 to soft reset chip)
    Por init:   0x40008081 (write with value 0x00 to refresh touch panel parameters)

    note: Por init and system soft reset should be used together to reset the chip and refresh touch panel data.