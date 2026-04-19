<!-- {{{
vim:ts=4:sw=4:sts=4:et:ai:si:fdm=marker:tw=100
}}} -->
# Useless box

## Introduction

This is the typical useless box. It's based on the thoughts and more or less the circuit of
[instructables.com's project](https://www.instructables.com/Arduino-Useless-Box/) although 99.lots%
of it is changed. It depends on the pieces from
[Jubaz' useless project](https://www.printables.com/model/190583-useless-box) (excuse the pun). This
one is just as useless as anything else out there, but I've added some more things.

Below is a description. It's a bit all over the place, but hell, I stared writing this and it was a
mess, so I tried to restructure it, but it didn't help much, but at least, it's not too long.

## Source code and program

In addition to the source code available on Jubaz' page, I've had a lot of help from my good friend
Claude to rewrite it so that it looks like real code (sorry to whoever wrote it). What's been added
is, in general:

 - A console
 - Three run modes:
    1. Round-robin (like the original)
    2. Round-robin first and then random
    3. Full random
 - Relay mode is included in the code at all time and settable from the console.
 - Whatever's set in the console, is saved to EEPROM (that is, it's not EEPROM, it's just flash
   emulating EEPROM, since people like to use that, for some reason).

The code is written in vsCode / PlatformIO, but it should work anywhere, really.  It's just C/C++.
This resides under the [source code directory](platformio/src/).

Note that by default, this uses LED pin 4, which is the built-in LED, and which is active low. This
is opposite of what's normal. Just set PIN_LED to whatever fits you and make sure to set INVERSE_LED
according to your schematic.

To use the serial console, I prefer **picocom**, but there's a gazillion of different ones out there.
For Windows, there's **putty** or the built-in **wt** (and others). On Lnux/Mac/Unix, check minicom
or picocom or even good old screen (and again, a ton of others).

By default, SERIAL_SPEED is set to 9600. I haven't bothered to allow this to be changed in the
console, but the code is open and it's just a macro. Regardless, it's not a lot to do there, you
won't notice much difference and 9600 works on anything from around 1962 and probably until
[GNU/Hurd is released](https://xkcd.com/1508/) or even after that. 

## Schematic

[The schematic](kicad/) is very simple and consists of the ESP8266, two resistors, two capacitors,
two servos and a switch. An external power switch is not included there - do it yourself! In
addition to that, tehre's the 7805 if you need to power it with more than 5V. The ESP8266 mini board
can handle 5V, but you'll let out the magic smoke if you go much higher.

With the 7805, you also need a couple of caps to stabilise things.

Note that if relay isn't enabled, you won't need R3 or Q1. Just remove E3 and connect negative of
the servos directly to GND instead of via the MOSFET. 

You may also want to increase C2 to 1mF (1000µF) to be on the safe side, but 470µF should do.

Also, if you don't need an external LED and are fine with the one onboard, just remove D1 and R1. If
you actually need an external LED, you will probably want to move away from D4, since that is
hardwired to the onboard one, which is inverted.

The scuematic isn't fully tested, meaning I haven't been running a simulation or anything like that
and not made a PCB for it, but it should work fine. 

[roy](mailto:roy@karlsbakk.net)

