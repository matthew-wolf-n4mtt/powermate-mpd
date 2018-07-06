/* powermate-mpd.h
* Music Player Daemon (MPD) Cilent that uses a Griffin PowerMate to control MPD.
*
* Version: 2.0.0
* Author:  Matthew J Wolf
* Date:    05-JUN-2018
* This file is part of Powermate-mpd.
* By Matthew J. Wolf <mwolf@speciosus.net>
* Copyright 2018 Matthew J. Wolf
*
* Powermate-mpd is free software: you can redistribute it and/or modify
* it under the terms of the GNU General Public License as published by the
* the Free Software Foundation,either version 2 of the License,
* or (at your option) any later version.
*
* Powermate-mpd is distributed in the hope that it will be useful,
* but WITHOUT ANY WARRANTY; without even the implied
* warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
* See the GNU General Public License for more details.
*
* You should have received a copy of the GNU General Public License
* along with the HPSDR-USB Plug-in for Wireshark.
* If not, see <http://www.gnu.org/licenses/>.
*
*/
#define BUFFER_SIZE 32
#define NUM_EVENT_DEVICES 16

#define NUM_VALID_PREFIXES 2

#ifndef MSC_PULSELED
// this may not have made its way into the kernel headers yet
#define MSC_PULSELED 0x01
#endif

#define LOCKFILE "/usr/local/var/run/powermate-mpd.pid"

struct items_status {
   char host[46];
   int port;
   int powermate_button;
   int down_rot;
   int mpd_paused;
   int random;
   time_t down_time;
} * items_status;

static const char *valid_prefix[NUM_VALID_PREFIXES] = {
  "Griffin PowerMate",
  "Griffin SoundKnob"
};

void monitor_powermate_mpd(int fd_powermate,int poll,
                           struct items_status *status);
void powermate_led_state(int fd_powermate,struct items_status *status);
void process_powermate_event(int fd, struct input_event *ev,
                             struct items_status *status);
int find_powermate(int mode);
int open_powermate(const char *dev, int mode);
void powermate_led(int fd, int state);
int AsciiDecCharToInt (char localLine[50], int start,int length);
void signal_handler(int signal);
void daemonize();
