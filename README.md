# OpenHAB CO2 sensor

This repository contains Cadsoft's Eagle schematic and PCBs as well as Arduino firmware for CO2 monitoring device based on ESP8266-01 and relatively cheap CO2 sensor MH-Z19. Device is designed to be powered from USB charger.

## Schematic & PCB

![alt](/eagle/co2_sch.png)

On schematic is nothing special. Two available pins of ESP01 module is used for serial communication with sensor at 9600 baud rate.

![alt](/eagle/co2_brd.png)

Board is designed as single-sided with some wire jumpers.

## Firmware

Firmware can be programmed via Arduino IDE. After device startup MH-Z19 sensor returns different values before it is heated up. Firmware first waits for sequence of 400PPM readings and after that for value different than 400PPM; then is sensor heated up and ready for measuring.

WIFI access point and network parameters can be set in source code. After device startup and sensor heated up is on his IP address available HTTP server, which provides measured CO2 PPM value as REST service. Via HTTP server is also available simple configuration page where some parameters can be changed.

Sensor is very sensitive so firmware averages 50 values (by default) for eliminate jitter. Number of averaged values, frequency of reading values from sensor and other parameters can be set in source file or via WEB configuration page.

## OpenHAB

Configuration in OpenHAB is very simple. First you need to define item (in your *.items file) with correct IP address of device.
```
Number co2PPM "Indoor CO2 [%s PPM]" <co2> { http="<[http://192.168.128.209/co2:15000:JS(intvalue.js)]" } 
```
After item definition is ready, OpenHAB reads every 15 seconds CO2 value from device. To display measured value you need modify your sitemap file. It's also very easy.
```
Text item=co2PPM
```
After successfull definition OpenHAB GUI shows simple text item:

![alt](/images/oh.png?raw=true)

## Case
In openscad directory can be found simple case for whole device. There is OpenSCAD source file as well as rendered STL model.

![alt](/openscad/case.png?raw=true)

## Some images
![alt](/images/2017-03-01%2017.21.12.jpg?raw=true)
Print is not perfect because my printer bed is too dirty. But this is not problem; device will be on cabinet.
![alt](/images/2017-03-02%2016.58.54.jpg?raw=true)
