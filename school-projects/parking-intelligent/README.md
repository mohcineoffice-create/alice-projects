# Smart parking with an automatic barrier (Arduino Uno)

An end-of-studies style project: two HC-SR04 ultrasonic sensors detect cars at the entrance and exit, an Arduino Uno counts the free places, drives a servo barrier and shows the state on a 16x2 LCD with a green/red LED and a buzzer.

The documents (specification, technical report, slides, jury Q&A, simulation guide) are written in **French**. See [`LISEZMOI.fr.md`](LISEZMOI.fr.md) for the full content list.

## Try it
- Open `06_Simulation_Wokwi/` and paste `sketch.ino` and `diagram.json` into a new Arduino Uno project at wokwi.com.
- Source: `Code/parking_intelligent/parking_intelligent.ino`. Precompiled HEX: `Proteus/HEX/`.

## What was verified
- The sketch compiles for the Arduino Uno with avr-gcc (about 6.2 KB of flash, 291 bytes of RAM).
- In Wokwi it starts and shows "Places: 4/4"; the entrance scenario lowers the count to 3/4 and moves the servo.
- Not verified: exit, full-parking and timed-closing scenarios in the simulator, any real hardware, and Proteus. Prices in the report are rough estimates.
