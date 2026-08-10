# G-Watch Paris

This watch is a personal project that I have worked on for fun over the last few years. The goal is to build a bluetooth smartwatch that has all the features that I would ever want on a watch, all in a compact format that I can actually wear around all day.

This is the third major code "refactor" (full code rewrite), named Paris because I was in Paris when I started working on it (I'm trying to do a cool version name thing). The goal of this version is to be more careful as I add features. I started with a solid base that fetched the time using wifi, and then used ESP32-S3 light sleep to turn off the cpu while asleep, which allowed for a three day battery life, but without being able to receive notifications over Bluetooth from my phone. Since then, I have been careful about optimizing all processes for efficiency, and eventually added Bluetooth Low Energy connectivity to my phone, using modem sleep on the ESP32-S3 to maintain a bluetooth connection while still using very low power.

<h2><a href="https://garrettjordan.xyz">Try it yourself in the LVGL simulator!</a></h2>

# Features
* 240x240 Touchscreen
* LVGL UI
* Linear brightness control (or exponential? your eyes are weird)
* USB-C Charging
* ~17 hour battery life
* Mediocre battery level estimation
* Bluetooth LE using Gadgetbridge
* Haptic Vibration Motor


# UI
* 5 unique watch faces
* Adjustable global accent color/theme (7 presets)
* Stopwatch, Timer, and Alarms
* Weather (from phone)
* Music Control (from phone, only visible while media is playing)
* Calculator
* IMU G-Force visualization
* Schedule (my current hardcoded class schedule, shows on compatible watch faces)
* Home assistant controls (also hardcoded)
* Dice (shake wrist to toss dice)
* Metronome
* Flashlight (White screen with max brightness)

# Gadgetbridge
<h2><a href="https://gadgetbridge.org">Gadgetbridge</a> • <a href="https://github.com/TurtleWhal/Gadgetbridge">G-Watch Fork</a></h2>

Initially, the G-Watch just emulated a Bangle.js watch, but now it is it's own device in Gadgetbridge via the custom fork

* **Stock Features**
  - Phone Notifications
  - Music Control
  - Weather
  - View watch battery level on phone
  - Set alarms from Phone
  - Find Phone from Watch
  - Find Watch from Phone
* **Added in the fork**
  - Notification icons (app icon or contact profile picture)
  - Music album art/cover
  - G-Watch device type


# Hardware
When I started this project, the [Lilygo T-Watch 2021][lilygo] was the only option on the market for a round screen + processor combo, but then Waveshare released their [ESP32-S3 Dev Board with 1.28in LCD][waveshare] which after desoldering some connectors is significantly thinner than the Lilygo option due to the more integrated processor.


**Specs (Waveshare)**
* ESP32-S3R2 240MHz SoC with 16MB of flash and 2MB of PSRAM
* GC9A01 1.28in round LCD with CST816S capacitive touchscreen
* QMI8658 6-axis IMU
* 600mAh LiPo Battery
* Small ERM Vibration Motor

[waveshare]: https://www.waveshare.com/esp32-s3-touch-lcd-1.28.htm
[lilygo]: https://lilygo.cc/products/t-watch-2021


# Construction
The shell of the watch is entirely 3D-Printed, I have made two different case styles, one that looks more like a modern smartwatch, with connectors for flexible 3d printed TPU Bands, and one that I designed to look like the [Minus-8 Diver 2.0](https://www.minus8watch.com/products/diver-2-0), with a band made of solid PLA links, and uses pieces of filament as the connection pins.

Onshape Links: [Diver](https://cad.onshape.com/documents/a453940ba5dcb4334617ea98/w/25dcbed7fc1405f6b1361e07/e/ce47f440421cf80b1a0c339c?renderMode=0&uiState=6a73b374ee169838c9393359), [Smartwatch](https://cad.onshape.com/documents/a453940ba5dcb4334617ea98/w/81b3cdb347ab8ccdde8f0e05/e/ce47f440421cf80b1a0c339c?renderMode=0&uiState=6a73b3e2ee169838c939350d)
