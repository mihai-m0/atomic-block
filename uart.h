#ifndef UART_H
#define UART_H

#include <stdint.h>

void uart_init();
void uart_putc(char c);
void uart_puts(char *s);
void uart_putc_pooling(char c);
void uart_puts_pooling(char *s);
void uart_put_u16(uint16_t value);

#endif