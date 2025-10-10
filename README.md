# TMF8829 (Time-of-flight) Arduino Uno driver  

This is a simple universal driver to show the capabilies of the time-of-flight devices TMF8829. 

## Requirements  

- an Arduino Uno R3 board 
- an ams TMF8829 Arduino Shield Board 
- a USB B cable
- the [Arduino IDE](https://www.arduino.cc/en/software) to compile and download the Arduino Uno example application


## Files  

The arduino project in the folder: **tmf8829** is a very simple single character command line interpreter listening/printing on UART
It contains the following files:

- **tmf8829.ino** - the arduino specific wrapper for the application
- **tmf8829_app.h** and **tmf8829_app.cpp** - the tmf8829 command line application
- **tmf8829.h** and **tmf8829.cpp** - the tmf8829 driver 
- **tmf8829_shim.h** and **tmf8829_shim.cpp** - a shim to abstract the arduino specific I2C, UART and GPIO functions
- **tmf8829_image.h** and **tmf8829_image.c** - the tmf8829 firmware that is downloaded by the driver as a c-struct


### Porting to another MCU

To port the driver and application to another platform you need to adapt the following files:
- **tmf8829.ino** - the arduino specific wrapper for the application, replace this with the requirements for your selected platform
- **tmf8829_shim.h** and **tmf8829_shim.cpp** - a shim to abstract the arduino specific I2C, UART and GPIO functions

### Demonstrator python scripts

There are also some python scripts: 
- **talk_to_arduino.py** that shows how to talk to the device in an automated manner. It will also dump all
 result records into the csv file: tmf8829_arduino_uno_file.csv.

To be able to run the python scripts you need first to program the Arduino Uno example application. The python scripts do
not do this.

If you do not have serial installed for python please do this: **pip install -r ./requirements.txt**

### Demonstrator with evm GUI and logger

First the zmq server must be started. The zmq server does the communication with the arduino driver.

The EVM GUI could be used to do the visualisation.
For the focal plane modes 32x32 and 48x32 it is necessary to use a high measurement period.
Otherwise the driver cannot read out all frames from the tmf8829 device and frames are lost, which means that the visualisation is not possible.
Visulisation of histograms is only possible for FP-mode 8x8 ond FP-mode 16x16 with less iterations and a high measurement period.

The logger could be used to store the data in json format.

Note: The default BAUDRATE with 2000000 could show communication errors.
In such a case, the zmq server reopens the connection to the Arduino.
To get rid of this issue, the BAUDRATE must be reduced (1000000 baud should be sufficient). 


#### Using the Demonstrator with an Arduino Zero

The Arduino Zero must be programmed with BAUDRATE 1000000.
The zmq server tries to find an Arduino Uno. If no device is found, the zmq server tries to find an Arduino Zero.

#### Using the Demonstrator with an other Arduino
The python sources for the zmq server could be downloaded.
The function **device_connected** could be extented for other devices.
Add the search for the new device. The USB VID:PID must be known. See the connected com ports, when the server is started.



## UART and command line interpreter  

The project listens and talks on the UART. Baud rate is 2000000, 8-bit, no parity, 1 stop bit.

The command line interpreter uses single characters followed by ENTER as input commands. 

UART commands
- h ... print help 
- d ... disable device
- e ... enable and download firmware
- p ... power down device
- w ... wakeup device
- m ... start measure
- s ... stop measure
- c ... use next configuration
- x ... clock correction on/off toggle
- a ... dump all registers
- z ... histogram dumping on/off
- + ... increase logging output
- - ... decrease logging output
- \# ... execute a reset on tmf8829

