# nrf52_vesc

## Table of contents
* [General info](#general-info)
* [Programming](#programming)
* [Power supply](#power-supply)
* [Useful Links](#useful-links)


## General info
This is code for the NRF52840 dongle (pca10059) for communicating between the VESC and VESC Tool (linux and mobile) over BLE. After uploading the firmware, the NRF can be connected to the VESC using the RX and TX pins chosen in main.c, and the BLE scanner in VESC Tool should be able to find it and connect. Note that the UART port on the VESC must be enabled with a baud rate of 115200 for this to work. The NRF can also communicate with the VESC Remote at the same time as it runs BLE.  

The code can be build with the NRF52 SDK by changing the path in Makefile.

## Programming
1. Download and install [nRF Connect for Desktop](https://www.nordicsemi.com/Software-and-Tools/Development-Tools/nRF-Connect-for-desktop).
2. Click `Add/remove apps` and install `Programmer` application.
3. Plug the dongle to USB port and click reset button to enter DFU mode. Red diode should start blinking.<img src="https://infocenter.nordicsemi.com/topic/ug_nrf52840_dongle/UG/common/images/nRF52840_dongle_press_reset.svg">
4. Select device from the list.
5. Click `Add HEX file` and select `nrf52840_xxaa.hex` from `_build` directory.
6. Click `Add HEX file` and select `s140_nrf52_6.1.1_softdevice.hex` from`SDK_ROOT\components\softdevice\s140\hex` directory.
7. Click `Write` to flash the device.


	

## Power supply
The nRF52840 Dongle can be powered from different sources.
* [Internal regulator (5 V)](https://infocenter.nordicsemi.com/topic/ug_nrf52840_dongle/UG/nrf52840_Dongle/hw_power_internal_regulator.html).
The default power supply of the nRF52840 Dongle is the USB interface. The USB interface supplies power to the on-chip high voltage regulator of the nRF52840 SoC. The USB power connection (VBUS) is also available along the board edge

* [External regulated source (1.8–3.6 V)](https://infocenter.nordicsemi.com/topic/ug_nrf52840_dongle/UG/nrf52840_Dongle/hw_power_ext_reg_source.html).
The nRF52840 Dongle can also be configured to be supplied from an external regulated `1.8–3.6 V` source through the `VDD OUT` connection point. To enable this, `SB2` must be cut and `SB1` must be soldered.


![alt text](https://infocenter.nordicsemi.com/topic/ug_nrf52840_dongle/UG/nrf52840_Dongle/Images/configuring_external_power_source.svg "external regulated 1.8–3.6 V")


After the code is uploaded, the NRF52 can be connected to the VESC in the following way:

| NRF52840 dongle       | VESC          |
| ------------- |---------------|
| GND           | GND           |
| VDD OUT/ VBUS*          | 3.3V/5V*    |
| TX (P0.29)     | RX            |
| RX (P0.31)   | TX            |

*The nRF52840 Dongle can be powered from different sources.


## Useful Links

* [nRF52840 Dongle Programming Tutorial](https://devzone.nordicsemi.com/nordic/short-range-guides/b/getting-started/posts/nrf52840-dongle-programming-tutorial)
* [nRF52840_Dongle_User_Guide_v1.1.pdf](https://infocenter.nordicsemi.com/pdf/nRF52840_Dongle_User_Guide_v1.1.pdf)
* [The VESC Project](https://vesc-project.com)


