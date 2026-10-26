# Atomicity: Corrupted 16 bit readings

Reproduces a race condition on purpose, then fixes it. A Timer1 ISR increments a 16-bit counter very fast while main reads it. Because the AVR is an 8-bit CPU, main's 16-bit read is two instructions, and the ISR can fire between them.

**Target:** ATmega328P (Arduino Uno), plain avr-gcc, direct register access.
**Depends on:** the interrupt-driven UART TX driver (`uart.c`/`uart.h`).

## Timer1 configuration (CTC mode)

| Setting | Value | Meaning |
|---|---|---|
| `TCCR1B` | `WGM12 \| CS10` | CTC mode (top = `OCR1A`), prescaler 1 |
| `OCR1A` | 99 | Compare value |
| `TIMSK1` | `OCIE1A` | Per-source enable for compare match A |
| Global enable | `sei()` | SREG bit I |

Interrupt frequency: `F_CPU / (prescaler x (OCR1A + 1))` = 16 MHz / 100 = **160 kHz** (every 6.25 us, 100 CPU cycles). The count runs 0..99, so 100 cycles, not 99.

## Test setup

```c
volatile uint16_t counter;          // incremented in ISR(TIMER1_COMPA_vect)
...
uint16_t now  = counter;            // read in main
uint16_t diff = now - prev;         // normally a small number
if (diff > 1000) { print BAD }      // impossible jump = torn read
```

## Version A: the bug

`now = counter;` compiles to two byte loads. If the timer interrupt lands between them while the low byte rolls over, main combines bytes from two different values.

Observed output:

```
BAD: prev=24063 now=23808
```

- `24063 = 0x5DFF`, `23808 = 0x5D00`
- Main read the high byte (`0x5D`) while the counter was `0x5DFF`
- The ISR ran: counter became `0x5E00`
- Main read the low byte: `0x00`
- Result: `0x5D00`, a value that never existed

Every BAD line has `prev` ending in `FF` and `now` ending in `00`: corruption only appears when the low byte rolls over.

## Version B: the fix

```c
#include <util/atomic.h>

uint16_t now;
ATOMIC_BLOCK(ATOMIC_RESTORESTATE)
{
    now = counter;
}
```

`ATOMIC_BLOCK` saves SREG, runs `cli()`, executes the block, and restores SREG on exit. `ATOMIC_RESTORESTATE` puts SREG back exactly as it was (unlike `ATOMIC_FORCEON`, which always re-enables interrupts).

Result: no BAD lines.

## How the fix behaves

- Only the global enable (SREG bit I) is cleared; `OCIE1A` stays set, so the hardware still sets the pending flag `OCF1A` during the block.
- The ISR runs right after the block ends: the interrupt is delayed, not lost.
- Only one event can be pending per flag, so a critical section longer than the interrupt period (100 cycles here) would lose ticks. Keep critical sections as short as possible.

## Key points

- Atomic means indivisible. An 8-bit access is atomic on AVR; 16-bit and wider accesses are not.
- A critical section is code that touches shared data and must not be interrupted.
- The CPU takes interrupts only between instructions, never inside one.

## Build and flash

```
make
make flash PORT=COM5
```

## References

- ATmega328P datasheet (DS40002061): "16-bit Timer/Counter1 with PWM" (CTC mode, TCCR1B, TIMSK1)
- avr-libc manual: `<util/atomic.h>`

- # Under the Hood: Disassembly of Interrupt Code

Disassembly analysis of the project to see what the compiler generates around an ISR: the vector table, register save/restore, SREG handling, and `reti`.

## Generating the listing

```
avr-objdump -d main.elf > main.lst
```

## Vector table (address 0)

- 26 `jmp` instructions, 4 bytes each: slot *n* is at byte address 4 x *n*
- `objdump` shows byte addresses; the datasheet lists word addresses (half the value)

| Slot | Byte address | Target |
|---|---|---|
| 0 | `0x00` | `__ctors_end` (Reset, startup code at `0x68`) |
| 11 | `0x2c` | `__vector_11` (Timer1 compare match A, the ISR from project 03) |
| 19 | `0x4c` | `__vector_19` (USART data register empty, UART TX ISR) |
| all others | | `__bad_interrupt` (catch-all, jumps to address 0 by default) |

`ISR(NAME_vect)` makes the compiler emit a function called `__vector_<n>`, which the linker places at slot *n*.

## Startup code at 0x68

```
eor r1, r1          ; r1 = 0 (avr-gcc expects r1 to always be zero)
out 0x3f, r1        ; SREG = 0 (all flags cleared, interrupts off)
ldi r28, 0xFF
ldi r29, 0x08
out 0x3e, r29       ; SPH
out 0x3d, r28       ; SPL  -> stack pointer = 0x08FF (RAMEND)
```

The stack starts at the top of RAM and grows downward.

## Timer1 ISR (`__vector_11`)

C source: `counter++;`

```
push r1                  ; save
push r0
in   r0, 0x3f            ; copy SREG
push r0                  ; save SREG
eor  r1, r1              ; r1 = 0 again
push r24
push r25
lds  r24, 0x0114         ; load counter low
lds  r25, 0x0115         ; load counter high
adiw r24, 1              ; 16-bit increment
sts  0x0115, r25         ; store high
sts  0x0114, r24         ; store low
pop  r25                 ; restore in reverse order
pop  r24
pop  r0
out  0x3f, r0            ; restore SREG
pop  r0
pop  r1
reti
```

- SREG is saved before any flag-changing instruction (`eor`, `adiw`)
- Only the registers the ISR uses are saved: r0, r1, r24, r25, and SREG
- Stack use: 2 bytes (hardware, return address) + 5 bytes (pushes) = 7 bytes

## UART ISR (`__vector_19`)

- Pushes the same set plus r30 and r31 (the Z pointer register pair, used to index `tx_buf`): 9 bytes of stack
- Empty buffer (`tx_head == tx_tail`): clears `UDRIE0` (bit 5 of `UCSR0B`) so the ISR stops re-firing
- Otherwise: reads `tx_buf[tx_tail]`, writes it to `UDR0`, advances `tx_tail` with wrap (`& 0x3F`, a 64-byte ring buffer)
- Both paths share one pop sequence ending in `reti`
- Registers `UCSR0B` (`0xC1`) and `UDR0` (`0xC6`) are in extended I/O space, so they need `lds`/`sts` instead of `in`/`out` (limited to `0x00..0x3F`)

## What the stack holds when an interrupt fires

- The hardware pushes only the return address (PC, 2 bytes) and clears the I bit
- Everything else (registers, SREG) is saved and restored by the ISR code, because main may be mid-computation and the ISR would otherwise overwrite its registers and flags

## `ret` vs `reti`

- Both pop the return address into the PC
- `reti` also sets the I bit again; with `ret`, interrupts would stay disabled after the first interrupt
- `reti` also guarantees one more instruction of the interrupted code executes before any pending interrupt is served
- Neither is written in the C source: `ISR()` makes the compiler end the function with `reti`

## References

- ATmega328P datasheet (DS40002061): "Interrupts" (Interrupt Vectors), "AVR CPU Core" (Reset and Interrupt Handling), "Instruction Set Summary" (EOR, RETI, LDS), "Register Summary"
- avr-libc manual: `<avr/interrupt.h>`
- GNU binutils manual: `objdump`
