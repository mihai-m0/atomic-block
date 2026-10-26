#include "uart.h"
#include <avr/io.h>
#include <avr/interrupt.h>

#define UART_BAUD  115200UL
#define TX_BUF_SIZE 64U
#define TX_BUF_MASK (TX_BUF_SIZE-1) 

static volatile uint8_t tx_buf[TX_BUF_SIZE];
static volatile uint8_t tx_head;
static volatile uint8_t tx_tail;
//volatile tells the compiler the value may change behind its back (here, inside the ISR),
//so it must re-read it from RAM every time instead of caching it in a register.


/*Techincal documentation:
https://ww1.microchip.com/downloads/en/DeviceDoc/Atmel-7810-Automotive-Microcontrollers-ATmega328P_Datasheet.pdf
*/

void uart_init(){
    //19.10.2 UCSRnA – USART Control and Status Register n A
    //Bit 1 – U2Xn: Double the USART Transmission Speed
    UCSR0A = (1<<U2X0);

    //19.10.5 UBRRnL and UBRRnH – USART Baud Rate Registers
    //Table 19-1. Equations for Calculating Baud Rate Register Setting
    UBRR0  = (uint16_t)((F_CPU / (8UL * UART_BAUD)) - 1UL);

    //19.10.4 UCSRnC – USART Control and Status Register n C
    //Bit 2:1 – UCSZn1:0: Character Size
    UCSR0C = (1 << UCSZ01) | (1 << UCSZ00);

    //19.10.3 UCSRnB – USART Control and Status Register n B
    //Bit 3 – TXENn: Transmitter Enable n
    UCSR0B = (1 << TXEN0);
}

void uart_putc_pooling(char c){
    /*
    The UDREn flag indicates if the transmit buffer (UDRn) is ready to receive new data. 
    If UDREn is one, the buffer is empty,and therefore ready to be written. 
    */
   while((UCSR0A & (1<<UDRE0)) == 0U);//is busy

   //19.10.1 UDRn – USART I/O Data Register n
   UDR0 = (uint8_t)c;
}

void uart_puts_pooling(char *s){
    while(*s != 0){
        uart_putc_pooling(*s);
        s++;
    }
}

void uart_putc(char c){
    uint8_t next = (uint8_t)((tx_head+1) & TX_BUF_MASK);
    while(next==tx_tail);

    tx_buf[tx_head]=(uint8_t)c;
    tx_head=next;

    //19.10.3 UCSRnB – USART Control and Status Register n B
    //Bit 5 – UDRIEn: USART Data Register Empty Interrupt Enable n
    UCSR0B |= (1<<UDRIE0);

}

void uart_puts(char *s){
    while(*s != 0){
        uart_putc(*s);
        s++;
    }
}

void uart_put_u16(uint16_t value)
{
    char digits[5];
    uint8_t n = 0U;

    do
    {
        digits[n] = (char)('0' + (value % 10U));
        value = value / 10U;
        n++;
    } while (value != 0U);

    while (n > 0U)
    {
        n--;
        uart_putc(digits[n]);
    }
}

ISR(USART_UDRE_vect){
    if(tx_head==tx_tail)//buffer empty
        UCSR0B &= (uint8_t)~(1<<UDRIE0);
    else{
        UDR0 = tx_buf[tx_tail];
        tx_tail=(uint8_t)((tx_tail+1) & TX_BUF_MASK);
    }
}

