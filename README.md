# Tap Beer Flow Control - Firmware

Firmware developed using ESP-IDF 5 for the ESP32 module to control the volume dispensed from a tap beer (or any other liquid, such as water).

View the developed hardware in this repository: [TapBeerVolumeControl_Hardware](https://github.com/conradoad/TapBeerVolumeControl_Hardware)

In the control part, the ESP32 module counts pulses from a simple flow sensor, converting them into volume units, and controls the flow by opening or closing a simple normally closed (NC) solenoid valve.

## Hardware Components
- ESP32 DevKitC

![image](https://github.com/conradoad/TapBeerVolumeControl_Firmware/assets/29844580/db13a53b-07f1-4f51-bf78-cfb2f3f90016)

- Flow Sensor

![image](https://github.com/conradoad/TapBeerVolumeControl_Firmware/assets/29844580/ab74c18c-ce00-41c9-9de3-dadd1bc7982a)

- Solenoid Valve

![image](https://github.com/conradoad/TapBeerVolumeControl_Firmware/assets/29844580/3be64cb3-19aa-4d34-be2e-7c64511d2974)

The firmware's interfacing part includes an HTTP server hosting a simple HTML+CSS+JS webpage and a REST API with a few endpoints for releasing the flow and calibrating.

## Interface

- Note 1: is the interface in portuguese because its for a Brazilian client.
- Note 2: it is only a POC, for validating and testing purpose

![image](https://github.com/conradoad/TapBeerVolumeControl_Firmware/assets/29844580/541143b0-7c00-452c-bb65-45fbab547dda)

In the operational part, you can set the volume to be released. The system opens the valve. If no starting flow is detected in 10 seconds, the valve is closed. Once started, the valve remains open until the released volume is fully consumed or if the flow is interrupted for 5 seconds.

The status message, consumed volume, and balance are updated in real-time through an open WebSocket.

In the calibration section, you have the option to input the accurate volume and adjust the proportional constant used for converting pulses to volume.

## Video

TODO: upload video

