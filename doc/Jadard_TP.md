## 12C address
    0x68(7bit)
    8bit address write: 0xD0
    8bit address read:  0xD1

## Back mode (normal operation)
    read register
    step 1: command:            F2 AA 55 0F F0 68
    step 2: read instruction:   F3 <read address>

    write register
    step 1: command:            F2 AA 55 0F F0 68
    step 2: write instruction:  F2 <write address>

## Non backdoor mode (not normally used)
    read register
    step 1: command  0x20011120
    can directly output the register at 0x20011120

    write register
    step 1: command 0x40008078 03
    which directly writes 0x03 (??? how does this work? how does the chip know if it's a read or write???)

## Touch panel interrupt usage
    Interrupt triggers on falling edge, and is auto cleared, while touch is detected, interrupt is continuously re-triggered.

## Other address
    Chip ID:    0x40008076 (read only, return 0x9032, can be used to check for for liveliness)
    Soft reset: 0x40008004 (write with value 0xA5 to soft reset chip)
    Por init:   0x40008081 (write with value 0x00 to refresh touch panel parameters)

    note: Por init and system soft reset should be used together to reset the chip and refresh touch panel data.