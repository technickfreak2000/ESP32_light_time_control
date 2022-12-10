# ESP32 RTC Light
Its an energy efficient timer.

Everything is based on the examples of the ESP-IDF projekt. 

DISCLAIMER: This project is far from perfect. Code is poorly written, just to make it work. PCB files (EasyEDA) are NOT correct at this time. I had a couble of issues and fixet it directly without updating the schematic!

## How does it work?
This project is supposed to consume as little power as possible, thus making it viable to run on batteries. 
Therefore, the famous AMS1117 3.3 got replaced with a sepic converter. Furtheremore you have another one at the output in order to have a steady and controllable output.
The onboard RTC clock will be updated over WiFi everytime the timer wakes it up or the ESP gets reset. 

## Setup
The ESP automatically creates an AP called "FBI Van" if it cannot connect to a WiFi network. It has an integrated capture portal where you can find the settings.
IP: 192.168.4.1

Furtheremore, you have a button and a status LED.
Hold the butten until the status led blinks:
1. Setup mode 
  * This mode automatically checks whether it can connect to your WiFi, otherwise it will create an AP.
2. Setup AP mode
  * This mode creates the AP right away.
3. Reset
  * This mode resets the internal settings to default.

The status led is fast flashing, when the ESP is in Setup mode. It automatically goes into deep sleep after 5 minutes.
