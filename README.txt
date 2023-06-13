==========
    he
==========

https://youtu.be/btHpHjabRcc

tour of the stuffs:
- embed: source code for ESP32 (little chip friend)
- esp-idf and esp-protocols: firmware for ESP32
- web: shortcut to the network authentication portal

to build the software in embed / reprogram the ESP:
- grab this repository: `git clone --recursive https://github.com/jaytlang/him`
- install python if you haven't yet...
- install esp-idf by opening up the command prompt, changing to the esp-idf
  subdirectory in this folder, and running install.bat. guessing you change
  directories with `cd` in the command prompt.
- activate esp-idf by running export.bat in this same folder
- navigate to the embed folder
- run idf.py set-target esp32-s3
- run idf.py menuconfig and behold the cute screen that appears
- head to the Partitions menu and select custom partitions, using a custom CSV
  file named "partitions.csv"
- back out to the main menu, then head to Component config > Driver
  Configurations > RMT Configuration. enable the "ISR IRAM safe" option.
- back out and save the configuration
- run `idf.py build`
- run `idf.py flash monitor` with the board plugged in
