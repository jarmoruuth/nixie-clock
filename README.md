# nixie-clock

Clock is built to drive 6 IN+14 nixie tubes display. It shows the time, room temperature and outdoor temperature.

# Hardware
Clock prototype is based on a Raspberry Pi board (yes, I know it is overkill), later version is built on ESP32.
The display is built with 6 IN-14 (ИН-14) soviet-era nixie tubes, driven by 2 soviet-era driver chips K155ID1 (K155ИД1 in Russian). These are analogs of SN74141.
Chips are controlled directly from the Raspberry Pi IO pins. The only reason why we have 2 driver chips is that it can only drive 10 digit pins, the second chip is only used to control 2 dot signs and 2 IN-3 bars between the digits.
The display anodes are selected using 6 TLP627 decouplers, controlled via 74HC238.

The clock is powered from a standard micro-USB connector located on the Raspberry Pi board.
The tubes require 200-300v voltage to operate, and that voltage is supplied from a charge pump, built on 555 and inductor, pushing from 5v from usb to 300v.

It features a DS18b20 digital thermometer to measure the inside temperature. 6 WS2812 leds are placed under the nixie tubes to backlight them, emulating hot cathodes.

References to hardware:
- https://www.raspberrypi.com/
- https://tubehobby.com/datasheets/in14.pdf
- https://eandc.ru/pdf/mikroskhema/k155id1.pdf
- https://neon1.net/nixieclock/sn74141.pdf
- https://www.soemtron.org/downloads/disposals/tlp627.pdf
- https://www.ti.com/product/CD74HC238
- https://www.analog.com/en/products/ds18b20.html
- https://cdn-shop.adafruit.com/datasheets/WS2812.pdf

Schematics and PCBs: https://oshwlab.com/alexander.krotov/in14

Wiring between the Raspberry Pi and the board:


# Software

The clock program source code is clock.c. Written in C, it requires libjson-c, libcurl, libws2811, and should be run as root to have permissions to access to GPIOs.

Clock displays the current system time, that could be with NTP, for example. The inside temperature is read from the hardware digital thermometer. The outside temperature
is read from openweathermap.org (you will need to get your own key and set OWM_KEY to use the service). libjson-c and libcurl are needed to get the outside temperature.

libws2811 is used to control 6 WS2812 leds.

nixie.service shows how to run the clock program from systemd.

# Design

Final clock video: https://www.youtube.com/watch?v=RONzVr5dUMM
