# ESP32 RTC Light
It's an energy efficient timer.

Everything is based on the examples of the ESP-IDF project. 

DISCLAIMER: This project is far from perfect. Code is poorly written, just to make it work. PCB files (EasyEDA) are NOT correct at this time. I had a couple of issues and fixed it directly without updating the schematic!

## How does it work?
This project is supposed to consume as little power as possible, thus making it viable to run on batteries. 
Therefore, the famous AMS1117 3.3 got replaced with a sepic converter. Furtheremore, you have another one at the output in order to have a steady and controllable output.
The onboard RTC clock will be updated over WiFi every time the timer wakes it up or the ESP gets reset. 

## Electrical requirements
The board needs about 2.8V minimum on the input in order to function and you schould not exceed 15V on the input and output!
I recommend that the board is supplied with 3V to 14V and that the output is set between 2.5V to 14V.
The minimum output voltage you would be able to achieve should be around 1.5V, but I haven't tested it.

## Setup
The ESP automatically creates an AP called "FBI Van" if it cannot connect to a WiFi network. It has an integrated capture portal where you can find the settings.
IP: 192.168.4.1

Furthermore, you have a button and a status LED.
Hold the button until the status led blinks:
1. Setup mode 
  * This mode automatically checks whether it can connect to your WiFi, otherwise it will create an AP.
2. Setup AP mode
  * This mode creates the AP right away.
3. Reset
  * This mode resets the internal settings to default.

The status led is fast flashing, when the ESP is in Setup mode. It automatically goes into deep sleep after 5 minutes.

Please be a bit patient. The Core clocks are set to low to save some power. 
