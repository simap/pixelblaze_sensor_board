Pixelblaze Sensor Expansion Board
-------------------

![sensor board](http://app.bhencke.com/pixelblaze/sb10.jpg)

* A microphone and signal processing with frequency magnitude data ranging from 37Hz-10KHz designed to work in very loud environments
* A 3-axis 16G accelerometer
* An ambient light sensor
* 5 analog inputs that can be used with potentiometers or other analog inputs

[For sale on Tindie](https://www.tindie.com/products/12158/)! Pick up this board and/or a Pixelblaze! Make cool sound-reactive LED things, or get one and hack the firmware!

![soundmatrix](http://app.bhencke.com/pixelblaze/soundmatrix.gif)

I hope you find this useful or interesting. Maybe you hack your board? I'd be stoked to hear about it! Roll your own? Great! Turn it into a robot brain? Awesome!

Shout out to Sparkfun, their boards were used in the initial prototype, and their Eagle files were really helpful!

Shout out to [https://github.com/pyrohaz/STM32F0-PCD8544GraphicAnalyzer](https://github.com/pyrohaz/STM32F0-PCD8544GraphicAnalyzer) which does similar FFT stuff with a similar processor, even shares the fixed point FFT code in common!

Non-Pixelblaze Usage
-------------------
Looking to use this with an Arduino or Teensy or something? You just need a free serial port at 115200 baud.

The protocol is fairly simple:

1. Each frame starts with "SB1.0" including a null character (6 bytes).
1. The frequency information follows, as 32 x 16-bit unsigned integers.
1. Then is the audio energy average, max frequency magnitiude, max frequency Hz, all 3 as 16-bit unsigned ints.
1. Next the accelerometer information as 3 x 16-bit signed integers.
1. Followed by the 5 x 16-bit analog inputs (12-bit resolution, shifted up to 16 bits)
1. Finally "END" including a null character (4 bytes).


License Information
-------------------
The hardware files are released under [Creative Commons ShareAlike 4.0 International](https://creativecommons.org/licenses/by-sa/4.0/) since the PCB is largely based off of Sparkfun boards with this license requirement.

Distributed as-is; no warranty is given.

The ElectroMage logo and wizard character is excluded from this license.

The software is released under The MIT License.

The MIT License

Copyright (c) 2018 Ben Hencke

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
THE SOFTWARE.