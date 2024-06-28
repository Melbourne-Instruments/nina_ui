/* midi test
sends a few midi notes on and off in a loop
*/

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <alsa/asoundlib.h>

#define MAX_MIDI_PORTS   4
void midi_route(snd_seq_t *seq_handle,int port);



/* Read events from writeable port and route them to readable port 0  */
/* if NOTEON / OFF event with note < split_point. NOTEON / OFF events */
/* with note >= split_point are routed to readable port 1. All other  */
/* events are routed to both readable ports.                          */
void midi_route(snd_seq_t *seq_handle, int port) {

    snd_seq_event_t ev;
    snd_seq_ev_clear(&ev);
    snd_seq_ev_set_direct(&ev);
    /* or */
    snd_seq_ev_set_subs(&ev);        /* send to subscribers of source port */

    snd_seq_ev_set_noteon(&ev, 0, 60, 127);
    snd_seq_event_output(seq_handle, &ev);
    snd_seq_drain_output(seq_handle);
    sleep(2);
    snd_seq_ev_set_noteon(&ev, 0, 67, 127);
    snd_seq_event_output(seq_handle, &ev);
    snd_seq_drain_output(seq_handle);
    sleep(3);

        snd_seq_ev_set_noteoff(&ev, 0, 67, 127);
    snd_seq_event_output(seq_handle, &ev);
        snd_seq_ev_set_noteoff(&ev, 0, 60, 127);
    snd_seq_event_output(seq_handle, &ev);
    snd_seq_drain_output(seq_handle);
    sleep(2);

}

int main(int argc, char *argv[]) {
        fprintf(stderr, "here \n ");
  snd_seq_t *seq;
  snd_seq_open(&seq, "default", SND_SEQ_OPEN_DUPLEX, 0);
      int port;
    port = snd_seq_create_simple_port(seq, "my port",
             SND_SEQ_PORT_CAP_READ|SND_SEQ_PORT_CAP_SUBS_READ,
              SND_SEQ_PORT_TYPE_APPLICATION);
  while (1) {
      midi_route(seq, port);
  }
}