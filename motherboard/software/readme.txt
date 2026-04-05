To program the motherboard (pcb with RP2040 chip), first put the device into divice mode,
then connect the board to the computer using the usb-c port.

Pico sdk should be installed, if you haven't added the --recursive flag while cloning the repo run:
git submodule update --init

To build run the following commands:
mkdir build
cd build
cmake ..
make -j4

Then to flash, inside the build folder, run:
make flash


If this doesn't work:
Hold the usb-boot button while plugging in the usb.
Then copy the motherboard.uf2 file, located in the build folder, to RP2040 external storage device.


The circuit located on the board is pretty much identical to the Rasberry Pi Pico,
so the usage should be the same, apart from the differences caused by the use of usb-c (device mode).
