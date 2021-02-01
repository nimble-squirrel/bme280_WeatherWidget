# bme280_WeatherWidget
## Desktop Weather
![alt text](docs/WeatherWidget_Desktop.png?raw=true)

## HOW TO SETUP

1. Connect Hardware as shown on the wiring diagram below

2. Compile and load kernel module:
```cmd
cd module
make all
./load
```
3. Copy WeatherWidget.desktop from App to your desktop
```cmd
cd ../App
cp WeatherWidget.desktop /home/pi/Desktop/
```

4. Edit WeatherWidget.desktop in order to use right paths
(Some pathes in WW.py can be also broken -> TODO)

```cmd
[Desktop Entry]
Name=Weather Widget
Icon=/home/pi/path/to/this/repo/icons/Sun2.png
Exec=python /home/pi/path/to/this/repo/App/WW.py
Terminal=false
Type=Application
```
5. After using unload kernel module
```cmd
./unlod
```

### Your Weather Widget - Tiny room Weather App on your Raspberry Pi.
![alt text](docs/WW_GUI.png?raw=true)

### Interacting
![alt text](icons/button_pressed.png?raw=true)



## What Hardware do you need:
- Raspberry Pi
- small breadboard
- BME I2C Sensor
- LED, Button, Resistor
- Some breadboard wires

![alt text](docs/Schaltplan.jpeg?raw=true)

