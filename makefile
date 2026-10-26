# Makefile - Stage 1c

MCU    = atmega328p
F_CPU  = 16000000UL
TARGET = main
PORT   = COM5
BAUD   = 115200

CC      = avr-gcc
SRC     = $(wildcard *.c)
OBJ     = $(SRC:.c=.o)

CFLAGS  = -mmcu=$(MCU) -DF_CPU=$(F_CPU) -Os -g -std=c11 \
          -Wall -Wextra -ffunction-sections -fdata-sections
LDFLAGS = -mmcu=$(MCU) -Wl,--gc-sections

all: $(TARGET).hex
	avr-size --mcu=$(MCU) -C $(TARGET).elf

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

$(TARGET).elf: $(OBJ)
	$(CC) $(LDFLAGS) $(OBJ) -o $@

$(TARGET).hex: $(TARGET).elf
	avr-objcopy -O ihex -R .eeprom $< $@

flash: $(TARGET).hex
	avrdude -p $(MCU) -c arduino -P $(PORT) -b $(BAUD) -U flash:w:$<:i

clean:
	del /Q *.o *.elf *.hex

.PHONY: all flash clean