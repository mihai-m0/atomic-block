#include <avr/io.h>
#include <avr/interrupt.h>
#include "uart.h"
#include <util/atomic.h>

volatile uint16_t counter = 0;

ISR(TIMER1_COMPA_vect)
{
    counter++;
}

int main(void)
{
    uart_init();

    TCCR1A = 0;
    TCCR1B = (1 << WGM12) | (1 << CS10);
    OCR1A  = 99;
    TIMSK1 = (1 << OCIE1A);

    sei();

    uint16_t prev = 0;

    while (1)
    {
        uint16_t now;
        ATOMIC_BLOCK(ATOMIC_RESTORESTATE)//turns off the bit 7 in SREG(Bit 7 – I: Global Interrupt Enable)
        {
            now = counter;
        }

        uint16_t diff = now - prev;
        if (diff > 1000)
        {
            uart_puts("BAD: prev=");
            uart_put_u16(prev);
            uart_puts(" now=");
            uart_put_u16(now);
            uart_puts("\r\n");
        }
        prev = now;
    }
}