==========
    he
==========

https://youtu.be/btHpHjabRcc

tour of the stuffs:
- cad: physical lamp design, self explanatory
- embed: source code for ESP32 (little chip friend)
- emulate: emulated LEDs to test WS2812s
- esp-idf and esp-protocols: firmware for ESP32
- web: wifi authentication portal html files (ewgh) (symlink into embed/)

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
- optionally tune the compiler options for performance. then re-save.
- run `idf.py build`
- run `idf.py flash monitor` with the board plugged in

notes on the cad:
- printed on a resin printer; no idea how well this would work on a
  regular 3d printer because i've never used one before. probably would be
  fine i guess, but the nice thing about the resin is the fine detail on e.g.
  the screw threads
- the top and bottom are printed with regular black resin, and screw together
  (incidentally, lefty tighty, oops). usually takes a few attempts to 'sand'
  down the threads and get a smooth tightening motion, but at least it works
- the 16-led neopixel ring snaps into the top and should be flush with the
  horizontal crossbar and the surface of the top (if slightly below the latter
  because my measurements are shit). there's a mm or two of clearance outside
  the ring to permit for future god awful soldering jobs, though the copy of
  the lamp you have isn't so bad
- the top is shit - lots of vertical overhangs that prove difficult for the
  printer to get right. you'll see these artifacts. please ignore them and
  pretend to be impressed by my beautiful handiwork (:
- the cat glues onto the top because i was too lazy to make that pretty
