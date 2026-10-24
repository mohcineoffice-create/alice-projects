#!/bin/bash
# Compile un croquis Arduino Uno avec avr-gcc (sans arduino-cli)
set -e
SK="$1"; OUT="$2"
CORE=/usr/share/arduino/hardware/arduino/avr
VAR=$CORE/variants/standard
LIBS="${LIBS_DIR:-./libs}"  # dossier contenant Servo/ et LiquidCrystal/ (clones de github.com/arduino-libraries)
B=$(mktemp -d)
FLAGS="-Os -mmcu=atmega328p -DF_CPU=16000000L -DARDUINO=10819 -DARDUINO_AVR_UNO -DARDUINO_ARCH_AVR -ffunction-sections -fdata-sections -Wall"
INC="-I$CORE/cores/arduino -I$VAR -I$LIBS/Servo/src -I$LIBS/LiquidCrystal/src"
for f in $CORE/cores/arduino/*.c; do avr-gcc -c $FLAGS -std=gnu11 $INC $f -o $B/$(basename $f).o; done
for f in $CORE/cores/arduino/*.S; do avr-gcc -c $FLAGS -x assembler-with-cpp $INC $f -o $B/$(basename $f).o; done
for f in $CORE/cores/arduino/*.cpp; do avr-g++ -c $FLAGS -std=gnu++11 -fno-exceptions $INC $f -o $B/$(basename $f).o; done
for f in $LIBS/Servo/src/avr/Servo.cpp $LIBS/LiquidCrystal/src/LiquidCrystal.cpp; do avr-g++ -c $FLAGS -std=gnu++11 -fno-exceptions $INC $f -o $B/$(basename $f).o; done
cp "$SK" $B/sketch.cpp; sed -i '1i #include <Arduino.h>' $B/sketch.cpp
avr-g++ -c $FLAGS -std=gnu++11 -fno-exceptions $INC $B/sketch.cpp -o $B/sketch.o
avr-gcc -Os -Wl,--gc-sections -mmcu=atmega328p -o $B/out.elf $B/*.o -lm
avr-size --mcu=atmega328p -C $B/out.elf
cp $B/out.elf "${OUT%.hex}.elf"
avr-objcopy -O ihex -R .eeprom $B/out.elf "$OUT"
echo "OK -> $OUT"
