  overo-irqlat
=============

Overview
-------

A linux module to measure gpio irq latency and the time it takes to toggle a 
gpio pin in an OMAP3 kernel.

The test requires an oscope to do the actual measurements and two gpio pins
jumpered together. The pins are assumed to be already mux'd as GPIO and not 
otherwise being used.

In the code the pins are labeled TEST_PIN and IRQ_PIN. They are interchangeable.

The example in the code is using a Gumstix Overo and two convenient pins on the
expansion board headers. Substitute different pins as needed. The definitions
are at the top of the source file.

    IRQ_PIN - GPIO_146
    TEST_PIN - GPIO_147

Jumper IRQ_PIN to TEST_PIN and hook an oscope probe to do the measurement
between them.


    IRQ_PIN ---- TP ---- TEST_PIN
                 |
               oscope


Build
-------

There is a file you can source to set up the environment for building using
the OE tools configured the standard gumstix way.

    $ git clone git://github.com/scottellis/overo-irqlat.git
    $ cd overo-irqlat
    $ source overo-source-me.txt
    $ make
 

Run
-------
 
Copy the irqlat.ko module to the board, insert it and then write 1 to do an 
irq latency test or anything else to do a gpio toggle speed test.

    root@overo:~# insmod irqlat.ko

This runs the irq latency test

    root@overo:~# echo 1 > /dev/irqlat

This runs the gpio toggle test

    root@overo:~# echo 2 > /dev/irqlat


Set your scope to trigger on a rising signal.

The program sets up IRQ_PIN as an input and irq enables it for an 
IRQ_TRIGGER_RISING signal.


Results
-------

The latest test was with a 2.6.36 kernel and two Overo COMs, one 720 MHz Tide
and one 500 MHz board.

The same tftp booted kernel, nfs root filesystem and Tobi expansion board was
used for both COMs. Only the boot.scr on the SD card specifying the different
mpurates was different.
 
For the irq latency test, the program raises TEST_PIN and in the IRQ_PIN irq 
handler, it lowers TEST_PIN again. The difference between the rise and fall of 
TEST_PIN is what I am calling the irq latency. This ignores the time the 
gpio_set() call takes, but you can see from the next test that it is negligble
for this measurement. 

I get values in the neighborhood of 8-9 usecs for either board. There is a good
amount of variation in this test. It sometimes takes up to 10 usecs for either 
board, but never under 8 usecs.

Refer to the *_gpio-irq-latency.png screenshots.

For the gpio toggle test, the IRQ_PIN irq handler is not enabled and not used
in the test. The module just runs a tight loop setting TEST_PIN high then low
again for 1000 iterations. You can watch with an oscope the time this takes.

Refer to the *-gpio-toggle-1000.png screenshots.

Here are the measurements I get.

500 MHz COM: ~420 usecs for 1000 cycles = ~210 nsecs to change state = ~4.76 MHz
720 MHz COM: ~290 usecs for 1000 cycles = ~145 nsecs to change state = ~6.90 MHz


See the source code for more details.

