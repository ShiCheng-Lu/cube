# Display Cube

You seen those anamorphic billboards that looks 3D from a certain perspective? Now it's a desktop cube, and it looks 3D from every perspective.

### Full stack!

The goal of the project is to build a cube that has three sides with displays, and one camera to track where your eye is. The displays can show images in just the right way depending on where your eye is to make something appear 3D.

### Hardware

The Raspberry Pi 5 only has 2 HDMI (micro) output ports, in order to drive all 3 displays, we need to leverage one of the two the MIPI outputs from the rpi. HOWEVER! the rpi uses a 22 pin MIPI output where as the display (and almost all DIY displays out there) uses 30 pins and requires some sort of LED driver to power the LED pins in the 30 pin input. The `mipi-adapter` directory contains the KiCad design to convert the 22 pin MIPI data output, along with a USB-C for power input into the 30 pin required for the display.

The hardware design accounts for impedance matching (100 ohm for the MIPI standard), which probably does not matter at all due to the frequency I'm running the MIPI data lines at to power my 720x720 output resolution. The USB-C plug is in fact properly connected with 2 5.1k resistors down to ground so it will work with any USB-C charger (unlike the initial batch of Raspberry Pi 4).

Another problem is that I have 3 displays and 1 Raspberry Pi to power, to accomodate this, I have designed a USB-C power splitter in `usb-power`. This board takes one USB-C input, connects up the VCC, GND, and the two CC power delivery negotiation pins into the Raspberry Pi so it can negotiate 5V 5A from the "Official Raspberry Pi Power Supply Unit". Unbeknownst to the Raspberry Pi and the power supply, there are 4 (for symmetry, I only need 3) USB plugs syphoning a couple amp on top of what the raspberry pi will use. This should be safe as long as my displays don't draw more than 2A total.

### Firmware

Initially I had high hopes that the display driver won't be an issue due to the display advertising the JD9365 driver, which has compatible drivers in the linux kernel. My disappointment quickly became inmeasurable and my day ruined, as I realized the displays are using the JD9365TX driver chip, which are totally incompatible with the JD9365D driver code. Luckly for me, the display supplier has provided me with all the resources the write a kernal driver for this display. With the help of my good friend Claude, there should be a working KMS display driver under the `mipi-driver` directory.

### Software

Rendering in C++ and vulkan. Build instructions are in [`app/README.md`](app/README.md).

### Harderware

The box: using FreeCAD
