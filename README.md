# Experimental home automation mesh network

[![Build Status](https://travis-ci.org/janLo/automation_mesh.svg?branch=master)](https://travis-ci.org/janLo/automation_mesh)

This might become a home automation mesh network consisting of
arduino (or bare ATMega) Microcontrollers, nrf2401 transceivers
and some actors/sensors.

* *firmware*: This is the firmware lib for the controllers.
* *meshnet*: This is the host service

## Firmware

The firmware is provided in two PlatformIO projects:

* *master_firmware*: The firmware for the master node that is connected to the "computer". It acts as mesh master/dhcp and proxies all packes over the serial connection to the service running on the "computer".
* *node_firmware*: The firmware for the actual sensor/actor nodes.

There is a script called `manage.py` in the firmaware directory to help to build the firmware.

## Scripts

* `debug_serial.py`: This is a mock for the serial connection to the network. The intention is to mock nodes to test the development without having the actual hardware at the hand.
