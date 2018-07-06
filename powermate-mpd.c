/* powermate-mpd.c
 * Music Player Daemon (MPD) Cilent that uses a Griffin PowerMate to control MPD
 *
 * Version: 2.0.0
 * Author:  Matthew J Wolf
 * Date:    06-JUL-2018
 *  This file is part of Powermate-mpd.
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
#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <syslog.h>
#include <time.h>
#include <unistd.h>
#include <linux/input.h>
#include <mpd/client.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <sys/types.h>
#include "./powermate-mpd.h"

int debug = 0;

pid_t pid, sid;
FILE *pidfile;

/*
 * Fuction : main
 * Desc    : The main fuction of the program.
 * Inputs  : Common arguments, the "--help" argument lists all the arguments.
 * Outputs :
 *            1. Errors sent to stderr and syslog.
 *            2. Process termination value.
 */
int main(int argc, char *argv[]) {

   int poll = 10; //sec between polls, 10 miniaum

   int i = -1;
   int fd_powermate = -1;

   const char *mpd_error;

   struct mpd_connection *mpd_conn = NULL;
   struct mpd_status *mpd_status = NULL;

   struct items_status *status = malloc(sizeof(struct items_status));

   // Set status struc initial values
   strcpy(status->host,"::1"); // MPD host
   status->port = 6600; //default MPD port
   status->powermate_button = 0;
   status->down_rot = 0;
   status->mpd_paused = 0;
   status->random = 0;

   for ( i=1; i < argc; i++ ) {
      if (!strcmp("-d",argv[i])) {
         debug = 1;
      }
      if (!strcmp("-h",argv[i])) {
         // MPD host
         if ( argv[i+1] != '\0' ) {
            strcpy(status->host,argv[i+1]);
         }
      }
      if (!strcmp("-p",argv[i])) {
         // MPD host port
         if ( argv[i+1] != '\0' ) {
            status->port = AsciiDecCharToInt(argv[i+1],0,(int)strlen(argv[i+1]));
         }
      }
      if (!strcmp("-P",argv[i])) {
         // MPD host poll interval
         if ( argv[i+1] != '\0' ) {
            poll = AsciiDecCharToInt(argv[i+1],0,(int)strlen(argv[i+1]));
            if ( poll < 10 ) {
               poll = 10;
            }
         }
      }
      if (!strcmp("--help",argv[i])) {
         // Display Usage
         printf("\nusage: powermate-mpd -dhpP --help\n"
                "----------------------------------------------\n"
                "-d Debug\n"
                "      Does not daemonize and displays messages\n"
                "-h MPD Host IP Address\n"
                "      Default: %s\n"
                "-p MPD Host Service Port\n"
                "      Default: %d\n"
                "-P MPD Polling Interval (Seconds)\n"
                "      Default and Minimum is 10 seconds\n"
                "--help Display the program usage details\n\n"
                ,status->host,status->port);
         return EXIT_SUCCESS;
      }
   }

   if (debug) {printf("Host: %s Port: %d Poll: %d\n",status->host,
                      status->port,poll); }

   openlog("powermate-mpd",LOG_PID, LOG_DAEMON);

   // Open Powermate read and write.
   fd_powermate = find_powermate(O_RDWR);
   if (fd_powermate < 0) {
      fprintf(stderr, "Unable to locate powermate.\n");
      syslog(LOG_ERR,"Unable to locate powermate.");
      exit (EXIT_FAILURE);
   }

   // Set Powermate LED when the program starts
   mpd_conn = mpd_connection_new(status->host, status->port, 30000);
   mpd_send_status(mpd_conn);
   mpd_status = mpd_recv_status(mpd_conn);

   if (mpd_connection_get_error(mpd_conn) != MPD_ERROR_SUCCESS) {
      mpd_error = mpd_connection_get_error_message(mpd_conn);
      fprintf(stderr, "Error: mpd connection: %s\n", mpd_error);
      syslog(LOG_ERR,"Error: mpd connection: %s", mpd_error);
      mpd_connection_free(mpd_conn);
      exit (EXIT_FAILURE);
   }

   switch (mpd_status_get_state(mpd_status)) {
   case MPD_STATE_STOP:
      if (debug) { printf("STOP LED Off\n"); }
      powermate_led(fd_powermate,0);
      break;
   case MPD_STATE_PLAY:
      if (debug) { printf("Play LED On\n"); }
      powermate_led(fd_powermate,1);
      break;
   case MPD_STATE_PAUSE:
      // Changes from paused to play.
      if (debug) { printf("Paused to Play: LED On\n"); }
      powermate_led(fd_powermate,1);
      mpd_send_toggle_pause(mpd_conn);
      break;
   case MPD_STATE_UNKNOWN:
      break;
   }

   mpd_connection_free(mpd_conn);

   // Fork Daemon
   if (!debug) {
      daemonize();
   }

   monitor_powermate_mpd(fd_powermate,poll,status);

   close(fd_powermate);

   exit(EXIT_SUCCESS);
}

/*
 * Fuction : monitor_powermate_mpd
 * Desc    : A fuction that monitors the powermate device for state changes.
 *           The fuction calls other fuctions to process the new state / event.
 * Inputs  :
 *          int fd_powermate - The powermate file descriptor.
 *          int poll         - The polling interval in seconds.
 *          struct *status   - A items_status structure that is defined in
 *                             local powermate.h
 * Outputs : Errors sent to stderr and syslog.
 */
void monitor_powermate_mpd(int fd_powermate,int poll,
                           struct items_status *status) {

   int i = -1;
   int rc = -1;
   int events = -1;

   fd_set set;

   struct input_event ibuffer[BUFFER_SIZE];
   struct timeval timeout;

   timeout.tv_sec = poll;

   for (;; ) {

      // Need to reset the FD set before each select call.
      FD_ZERO(&set);
      FD_SET(fd_powermate,&set);
      timeout.tv_sec = poll;

      rc = select(fd_powermate+1,&set,NULL,NULL,&timeout);

      if ( rc == 0 ) { // Select Timeout
         // Query MPD and update Powermate LED
         if (debug) { printf("Select Timeout\n"); }
         powermate_led_state(fd_powermate,status);
         continue;
      } else if ( rc == -1 ) {
         fprintf(stderr,"Select Error\n");
         syslog(LOG_ERR,"Select Error");
      }

      if (FD_ISSET(fd_powermate,&set)) {
         rc = read(fd_powermate, ibuffer,
                   sizeof(struct input_event) * BUFFER_SIZE);
         if ( rc > 0 ) {
            events = rc / sizeof(struct input_event);
            for (i=0; i<events; i++) {
               process_powermate_event(fd_powermate,&ibuffer[i],status);
            }
         } else {
            fprintf(stderr, "read() failed: %s\n", strerror(errno));
            syslog(LOG_ERR,"read() failed: %s", strerror(errno));
            return;
         }
      }

   }

   return;
}

/*
 * Fuction : powermate_led_state
 * Desc    : A fuction that changes the state of the powermate's LED.
 * Inputs  :
 *          int fd           - The powermate file descriptor.
 *          struct *status   - A items_status structure that is defined in
 *                             local powermate.h.
 * Outputs : Errors sent to stderr and syslog.
 */
void powermate_led_state(int fd,struct items_status *status) {
   const char *mpd_error;

   struct mpd_connection *mpd_conn = NULL;
   struct mpd_status *mpd_status = NULL;

   mpd_conn = mpd_connection_new(status->host, status->port, 30000);

   if (mpd_connection_get_error(mpd_conn) != MPD_ERROR_SUCCESS) {
      mpd_error = mpd_connection_get_error_message(mpd_conn);
      fprintf(stderr, "Error: mpd connection: %s\n", mpd_error);
      syslog(LOG_ERR,"Error: mpd connection: %s", mpd_error);
      if (debug) { fprintf(stderr,"%s\n",mpd_error); }
      mpd_connection_free(mpd_conn);
      return;
   }

   mpd_send_status(mpd_conn);
   mpd_status = mpd_recv_status(mpd_conn);

   switch (mpd_status_get_state(mpd_status)) {
   case MPD_STATE_STOP:
      if (debug) { printf(" LED: Stop\n"); }
      powermate_led(fd,0);
      break;
   case MPD_STATE_PLAY:
      if (debug) { printf(" LED: Play\n"); }
      status->mpd_paused = 0;
      powermate_led(fd,1);
      break;
   case MPD_STATE_PAUSE:
      if (debug) { printf(" LED: Pause\n"); }
      status->mpd_paused = 1;
      powermate_led(fd,3);
      break;
   case MPD_STATE_UNKNOWN:
      if (debug) { printf(" LED: UNKNOWN\n"); }
      break;
   }

   mpd_connection_free(mpd_conn);
}

/*
 * Fuction : process_powermate_event
 * Desc    : A fuction that takes some action when the state of the powermate
 *           changes.
 * Inputs  :
 *          int fd           - The powermate file descriptor.
 *          struct *ev       - A input_event structure. The structure is defined
 *                             in linux/input.h.
 *          struct *status   - A items_status structure that is defined in
 *                             local powermate.h.
 * Outputs : Errors sent to stderr and syslog.
 */
void process_powermate_event(int fd, struct input_event *ev,
                             struct items_status *status) {

   const char *mpd_error;

   struct mpd_connection *mpd_conn = NULL;
   struct mpd_status *mpd_status = NULL;

   time_t up_time;

   mpd_conn = mpd_connection_new(status->host, status->port, 30000);

   if (mpd_connection_get_error(mpd_conn) != MPD_ERROR_SUCCESS) {
      mpd_error = mpd_connection_get_error_message(mpd_conn);
      fprintf(stderr, "Error: mpd connection: %s\n", mpd_error);
      syslog(LOG_ERR,"Error: mpd connection: %s", mpd_error);
      if (debug) { fprintf(stderr,"%s\n",mpd_error); }
      mpd_connection_free(mpd_conn);
      return;
   }

   switch (ev->type) {
   case EV_REL:
      if (ev->code == REL_DIAL) {

         if (debug) {
            printf(" -Button %s %d: was rotated %d units.\n",
                   status->powermate_button ? "down" : "up",
                   status->powermate_button,
                   (int)ev->value);
         }

         if (status->powermate_button == 1 ) {

            // Rotation is too sensitive.
            // Change Item in play list for every other rotation.
            // Bitwise OR of random varies between two values (-1,0)
            status->random = ~status->random;
            if (debug) {printf("  -Random: %d\n",status->random); }
            if (status->random == 0) {

               status->down_rot = 1;
               if ((int)ev->value > 0) {
                  if (debug) {printf("   -Next: in play list\n"); }
                  mpd_send_next(mpd_conn);
               } else if ((int)ev->value < 0) {
                  if (debug) {printf("   -Previous: in play list\n"); }
                  mpd_send_previous(mpd_conn);
               }
            }
         } else {
            if (debug) {printf("  -Volume Change %d\n",(int)ev->value); }
            mpd_send_change_volume(mpd_conn,(int)ev->value);
         }

      }
      break;

   case EV_KEY:
      if (ev->code == BTN_0) {
         switch (ev->value) {
         case 0:
            if (debug) { printf("Button UP\n"); }
            status->powermate_button = 0;
            if (status->down_rot == 1) {
               status->down_rot = 0;
               break;
            }

            up_time = time(0);

            if ( difftime(up_time,status->down_time) > 1 && ev->code
                 != REL_DIAL ) {
               if (debug) { printf(" -Button Down Long\n"); }
               mpd_send_status(mpd_conn);
               mpd_status = mpd_recv_status(mpd_conn);
               switch (mpd_status_get_state(mpd_status)) {
               case MPD_STATE_STOP:
                  if (debug) { printf("  -Play\n"); }
                  mpd_send_play(mpd_conn);
                  powermate_led(fd,1);
                  break;
               case MPD_STATE_PLAY:
                  if (debug) { printf("  -Stop\n"); }
                  mpd_send_stop(mpd_conn);
                  powermate_led(fd,0);
                  break;
               case MPD_STATE_PAUSE:
                  if (debug) { printf("  -Pause\n"); }
                  mpd_send_toggle_pause(mpd_conn);
                  break;
               case MPD_STATE_UNKNOWN:
                  if (debug) { printf("  -UNKNOWN\n"); }
                  break;
               }

            } else {
               if (debug) { printf(" -Button Down Short (tap)\n"); }
               mpd_send_toggle_pause(mpd_conn);
               switch (status->mpd_paused) {
               case 0:
                  printf("  -LED: Paused\n");
                  status->mpd_paused = 1;
                  powermate_led(fd,3);
                  break;
               case 1:
                  printf("  -LED: Un-Paused\n");
                  status->mpd_paused = 0;
                  powermate_led(fd,2);
                  break;
               }
            }
            break;
         case 1:
            if (debug) { printf("Button Down\n"); }
            status->powermate_button = 1;
            status->down_time = time(0);
            break;
         }

      }

   }
   if (debug) { fflush(stdout); }

   mpd_connection_free(mpd_conn);
}

/*
 * Fuction : find_powermate
 * Desc    : A fuction that finds the powermate device.
 * Inputs  : int mode - File descriptor "file status" flags.
 * Outputs : The file descriptor for the powermate device. When no powermate
 *           device was found the file descriptor value is "-1".
 * Source  : The William Sowerbutts's Linux PowerMate driver.
 */
int find_powermate(int mode) {
   char devname[256];
   int i, r;

   for (i=0; i<NUM_EVENT_DEVICES; i++) {
      sprintf(devname, "/dev/input/event%d", i);
      r = open_powermate(devname, mode);
      if (r >= 0) {
         return r;
      }
   }

   return -1;
}

/*
 * Fuction : open_powermate
 * Desc    : A fuction that opens the file descriptor for the powermate device.
 * Inputs  :
 *           char *dev - Filesystem device file
 *           int mode  - File descriptor "file status" flags.
 * Outputs : The file descriptor for the powermate device. When no powermate
 *           device was found the file descriptor value is "-1".
 * Source  : The William Sowerbutts's Linux PowerMate driver.
 */
int open_powermate(const char *dev, int mode) {
   int fd = open(dev, mode);
   int i;
   char name[255];

   if (fd < 0) {
      if (debug) {
         fprintf(stderr, "Unable to open \"%s\": %s\n", dev, strerror(errno));
      }
      return -1;
   }

   if (ioctl(fd, EVIOCGNAME(sizeof(name)), name) < 0) {
      if (debug) {fprintf(stderr, "\"%s\": EVIOCGNAME failed: %s\n", dev,
                          strerror(errno)); }
      close(fd);
      return -1;
   }

   // it's the correct device if the prefix matches what we expect it to be
   for (i=0; i<NUM_VALID_PREFIXES; i++)
      if (!strncasecmp(name, valid_prefix[i], strlen(valid_prefix[i]))) {
         return fd;
      }

   close(fd);
   return -1;
}

/*
 * Fuction : powermate_led
 * Desc    : A fuction that interfaces with the powermate's LED.
 * Inputs  :
 *           int fd    - File descriptor "file status" flags.
 *           int state - New LED state.
 * Outputs : Errors sent to stderr and syslog.
 * Source  : The fuction is an adaptation of William Sowerbutts's
 *           Linux PowerMate driver.
 */
void powermate_led(int fd, int state) {

   struct input_event ev;
   memset(&ev, 0, sizeof(struct input_event));

   int static_brightness = 0x0;
   int pulse_speed = 255;
   int pulse_table = 0;
   int pulse_asleep = 0;
   int pulse_awake = 0;

   switch (state) {
   case 0:
      // Stop
      static_brightness = 0;
      pulse_awake = 0;
      break;
   case 1:
      // Play
      static_brightness = 255;
      pulse_awake = 0;
      break;
   case 2:
      // Pause Off
      static_brightness = 255;
      pulse_awake = 0;
      break;
   case 3:
      // Pause On
      pulse_speed = 260;
      pulse_awake = 1;
      break;
   }

   static_brightness &= 0xFF;

   if(pulse_speed < 0)
      pulse_speed = 0;
   if(pulse_speed > 510)
      pulse_speed = 510;
   if(pulse_table < 0)
      pulse_table = 0;
   if(pulse_table > 2)
      pulse_table = 2;
   pulse_asleep = !!pulse_asleep;
   pulse_awake = !!pulse_awake;

   ev.type = EV_MSC;
   ev.code = MSC_PULSELED;
   ev.value = static_brightness | (pulse_speed << 8) | (pulse_table << 17)
              | (pulse_asleep << 19) | (pulse_awake << 20);

   if (write(fd,&ev,sizeof(struct input_event)) != sizeof(struct input_event)) {
      fprintf(stderr, "write(): %s\n", strerror(errno));
      syslog(LOG_ERR,"write(): %s\n", strerror(errno));
   }

}

/*
 * Fuction : AsciiDecCharToInt
 * Desc    : A fuction that converts a ASCII charater string to an "int".
 * Inputs  :
 *           char localLine - String of ASCII charaters.
 *           int start     - The string array index value for the frist charater
 *           int length    - The length of the ASCII charater string.
 * Outputs : int out - The "int" value for the input ASCII charater string.
 */
int AsciiDecCharToInt (char localLine[50], int start,int length) {
   int i = 0;
   int tmp = -1;
   int out = 0;

   for (i=0; i<length; i++) {

      tmp =  localLine[start+i] -'0';

      if ( i == (length-1) ) {
         out = out + tmp;
      } else {
         out = out + tmp;
         out = out * 10;
      }

   }

   return out;
}

/*
 * Fuction : signal_handler
 * Desc    : The signal handler fuction that is registered with system kernel
 *           via a sigaction structure.
 * Inputs  : int signal - The system signal sent to the running process.
 * Outputs : Process termination value.
 */
void signal_handler(int signal) {

   switch (signal) {
   case SIGTERM:
      syslog(LOG_NOTICE,"Received SIGTERM: Exiting");
      unlink(LOCKFILE);
      exit(EXIT_SUCCESS);
      break;
   case SIGINT:
      syslog(LOG_NOTICE,"Received SIGINT: Exiting");
      unlink(LOCKFILE);
      exit(EXIT_SUCCESS);
      break;
   case SIGKILL:
      syslog(LOG_NOTICE,"Received SIGKILL: Exiting");
      unlink(LOCKFILE);
      exit(EXIT_SUCCESS);
      break;
   }

}

/*
 * Fuction : daemonize
 * Desc    : A fuction the daemonizes the process.
 * Inputs  : None
 * Outputs : None - A child process.
 */
void daemonize() {
   int lf_fd;
   char buf[16];
   pid_t pid, sid;

   struct flock lf_flock;
   struct rlimit rl;
   struct sigaction sa;

   // Change the file mode mask
   umask(0);

   // Get the max limit of file descriptors
   if( getrlimit(RLIMIT_NOFILE, &rl) < 0 ) {
      syslog(LOG_ERR,"Can get file limit: %s",strerror(errno));
      exit(EXIT_FAILURE);
   }

   // Add comment
   if ( (pid = fork()) < 0 ) {
      syslog(LOG_ERR,"Unable to create child process: %s",
             strerror(errno));
      exit(EXIT_FAILURE);
   }

   if (pid > 0) { // Parent Exit
      exit(EXIT_SUCCESS);
   }

   // Get new session ID for child process.
   // Become session lead to drop controlling TTY.
   if ( (sid = setsid()) < 0 ) {
      syslog(LOG_ERR,"Child session ID error: %s",strerror(errno));
      exit(EXIT_FAILURE);
   }

   // Chnage working directory to root. This do that the daemon process
   // will not be able to make and file system changes or unmount any file
   // system.
   if (chdir("/") < 0 ) {
      syslog(LOG_ERR,"Can change working directory to /: %s",
             strerror(errno));
      exit(EXIT_FAILURE);
   }

   // Setup signal handler with system kernel.
   // Clear the signal mask so that no new TTYs will be opened.
   sa.sa_handler = signal_handler;
   sigemptyset(&sa.sa_mask);
   sa.sa_flags = 0;
   sigaction(SIGTERM,&sa,NULL);
   sigaction(SIGINT,&sa,NULL);
   sigaction(SIGKILL,&sa,NULL);

   // Create lock / pid file.
   lf_fd = open(LOCKFILE, O_RDWR | O_CREAT,
                S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);
   if (lf_fd < 0) {
      syslog(LOG_ERR,"Can not open lock file %s: %s",LOCKFILE,strerror(errno));
      exit(EXIT_FAILURE);
   }

   // Lock the lock / pid file
   lf_flock.l_type = F_WRLCK;
   lf_flock.l_start = 0;
   lf_flock.l_whence = SEEK_SET;
   lf_flock.l_len = 0;

   if ( fcntl(lf_fd, F_SETLK, &lf_flock) < 0 ) {
      if (errno == EACCES || errno == EAGAIN) {
         syslog(LOG_ERR,"Lock file access issue %s: %s",LOCKFILE,strerror(errno));
         close(lf_fd);
         exit(EXIT_FAILURE);
      }
      syslog(LOG_ERR,"Can not lock %s: %s",LOCKFILE,strerror(errno));
      close(lf_fd);
      exit(EXIT_FAILURE);
   }

   // Write the process PID into the lock file.
   ftruncate(lf_fd,0);
   sprintf(buf,"%ld", (long)getpid());
   write(lf_fd,buf,strlen(buf)+1);

   // Close out the standard file descriptors
   close(STDIN_FILENO);
   close(STDOUT_FILENO);
   close(STDERR_FILENO);

   syslog(LOG_NOTICE,"Start Up");

   return;
}
