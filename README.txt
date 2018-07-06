06-JULY-2018 Matthew J. Wolf <matthew.wolf.hpsdr@speciosus.net>

Powermate-MPD is a Music Player Daemon (MPD) client that uses a 
Griffin Powermate to control and display the status of the playback
of an MPD instance.

Parts of the program where adapted from William Sowerbutts's 
Linux PowerMate driver.

The program is written to run with Linux. 

URL for Griffin PowerMate - https://griffintechnology.com/us/powermate
URL for MPD - https://www.musicpd.org/

The Griffin PowerMate is a USB attached device that has one button, 
one rotary encoder, and one LED. 

Actions Taken for PowerMate Inputs
----------------------------------
-When the button is tapped:
	 The MPD playback is paused or un-paused.

-When the button is pushed down for over around three seconds:
	The MPD playback is started or stopped.

-When the Powermate is rotated:
	Rotated Right: The audio volume is increased.
	Rotated Left:  The audio volume is decreased.	    
 
-When the button is pushed and the Powermate is rotated:
	Button Down and Rotated Right: Move forward in the play list.
	Button Down and Rotated Left:  Move backwards in the play list.	 

LED Behavior
------------
The LED is changed with Powermate input. The LED also changes independent of
PowerMate input. The program polls MPD for it's current state.

MPD Playback Stopped:    LED is OFF
MPD Playback is Paused:  LED is BLINKING
MPD Playback is Playing: LEN in ON

Program Options and Defaults
----------------------------
-d Debug
	Does not daemonize and displays message.
-h MPD Host IP Address
	The default host address is ::1.
        ::1 is the IPv6 local host loop-back address
-p MPD Host Service Port
	The MPD host service port is 6600.
-P MPD Polling Interval (Seconds)
        Default and Minimum is 10 seconds.
--help 
	Display the program usage details

Required Libraries
------------------
Core C library
MPD Client library
- https://www.musicpd.org/libs/libmpdclient/
 
PowerMate Device File
---------------------
The program examines the text string of USB bus details of the devices.
The program works with devices that have the text strings of:
"Griffin PowerMate" and "Griffin SoundKnob".

I also used a udev rule to to create the /dev/input/powermate symlink to
the input device file.

The udev rule - /etc/udev/rules.d/61-powermate.rules:   
ACTION=="add", ENV{ID_USB_DRIVER}=="powermate", SYMLINK+="input/powermate", MODE="0666"

The udev  directory in the source contains an example udev rules file.

Systemd
-------
The systemd directory in the source contains an example systemd service file. 
The example file uses the default MPD host IP address, service port and polling
interval. 

Logic Diagram
-------------
The doc directory in the source contains a pdf file. In the file is a high 
level digram of the logic that is used when there is a PowerMate input
event.   


