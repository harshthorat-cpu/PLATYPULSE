# PLATYPULSE

### SRA Eklavya 2026 Project - Platypulse

![ESP32-S3](https://img.shields.io/badge/ESP32--S3-E7352C?style=for-the-badge&logo=espressif&logoColor=white)
![ESP-IDF](https://img.shields.io/badge/ESP--IDF-000000?style=for-the-badge&logo=espressif&logoColor=white)
![KiCad](https://img.shields.io/badge/KiCad-314CB0?style=for-the-badge&logo=kicad&logoColor=white)
![C](https://img.shields.io/badge/C-00599C?style=for-the-badge&logo=c&logoColor=white)


<img width="966" height="540" alt="Screenshot 2026-08-29 at 9 50 27 PM" src="https://github.com/user-attachments/assets/c3281e35-4fd6-4986-8fc6-04aece5b865a" />

> A unified handheld platform for RF, RFID/NFC, and IR wireless experimentation — built on the ESP32-S3.

## Problem Statement

Understanding and experimenting with different wireless communication technologies often requires multiple dedicated tools, modules, and software environments. RF, RFID/NFC, and IR each operate differently and use different communication methods, making it difficult to explore them together in a simple and accessible setup.

## Our Solution

*One board, three wireless worlds — RF, RFID/NFC, and IR, brought together.*

Platypulse is designed as a unified handheld platform that brings RF, RFID/NFC, and IR capabilities into a single embedded system. Built around the ESP32-S3, it provides dedicated hardware for interacting with different wireless technologies while offering a common interface for capturing, analyzing, storing, and transmitting supported signals. The goal is to make wireless experimentation more accessible by combining multiple capabilities into one compact platform for learning, prototyping, and development.

## Features

*Everything you need to capture, analyze, and replay wireless signals — in one device.*

- **RFID/NFC**: Read, write, and manage supported cards, tags, and stickers.
- **UID Support**: Modify the UID of compatible changeable-UID cards.
- **IR Remote**: Learn, store, and replay IR signals.
- **Sub-GHz RF**: Receive, store, analyze, and transmit supported RF signals.
- **Custom PCB**: Purpose-built PCB integrating the core communication modules.
- **Custom UI**: Unified interface for controlling and managing all major functions.

## Prerequisites

*What you'll need on the hardware and software side to build and run Platypulse.*

### Hardware

- ESP32-S3 development environment
- PN532 RFID/NFC module
- CC1101 Sub-GHz RF module
- IR Receiver & IR LED
- TFT Display

### Software

- **ESP-IDF** — firmware development for the ESP32-S3
- **GNU Radio / Software Defined Radio (SDR)** — RF signal testing and analysis
- **KiCad** — schematic design and PCB development
- **Onshape** — mechanical and enclosure design

## Media

*See Platypulse in action across NFC, RF, and IR.*

**Connecting to Wi-Fi via NFC** — demonstrates connecting to Wi-Fi directly using a single NFC sticker containing the required instructions, eliminating the need to manually enter the Wi-Fi password and enabling automated tasks.


https://github.com/user-attachments/assets/eea3f232-0957-4e07-8702-c358589d3c55



**Transmitting an RF Signal** — demonstrates the transmission of an RF signal through the CC1101 module.



https://github.com/user-attachments/assets/e0021282-87bb-4f81-9ff2-c89db1f22ec8



**Receiving an RF Signal** — demonstrates the process of receiving an RF signal through the CC1101 module.


https://github.com/user-attachments/assets/8b9e34c6-dc06-4a26-969a-4266ece2f23d



**Capturing & Emulating an IR Signal** — demonstrates capturing and emulating an IR signal, used here to change the AC temperature.


https://github.com/user-attachments/assets/e1bdd879-e367-4fc6-8ea2-27d7d49550e6


## Repository Structure 
```c
PLATYPULSE
├── Docs/
│   └── Platypulse_Report 
├── Firmware/
│   ├── CC1101/                             
│   │   ├── cc1101_ook_rx/
│   │   │                        
│   │   ├── gnu-radio-scripts/
│   │   └── register_test/
│   │       
│   │                            
│   ├── IR/                             
│   │   ├── Receiver/
│   │   │   └── receiver_test/
│   │   ├── ir_transmitter/
│   │   │                         
│   │   └── store_and_emulate/
│   │                        
│   ├── PN532/                               
│   │   └── testingcode/
│   │                         
│   └── UI/                                  
│       └── ui_control/
│           
│                             
├── PCB/
│   └── kicad/
│                               
│           
├── media/                                   
├── .gitignore
└── README.md
```
## Installing

*Get up and running with Platypulse in three simple steps.*

1. Cloning the repo

```c
git clone https://github.com/harshthorat-cpu/PLATYPULSE.git
```

2. Working on the particular folder

```c
cd PLATYPULSE
cd <folder name you want>
```

3. Running the code

```c
idf.py set-target esp32s3
idf.py build
idf.py flash
```

## Authors



- [Harsh Thorat](https://github.com/harshthorat-cpu)
- [Lucky Belel](https://github.com/belellucky-15)

## Acknowledgement

*With gratitude to the tools, community, and mentors who made this possible.*

- Thanks for the ESP-IDF, KiCad, GNU Radio, and Onshape for their excellent tools that helped us make our project.
- Thanks to our Robotics Club [SRA-VJTI](https://github.com/sra-vjti).
- Special thanks to our mentors [Pushkar Dube](https://github.com/pushkardube), [Lakshya Lalwani](https://github.com/Lakshyaa1), and [Sarvaarth Narang](https://github.com/SarvaarthN).