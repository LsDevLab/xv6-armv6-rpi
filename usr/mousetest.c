#include "types.h"
#include "stat.h"
#include "user.h"
#include "fcntl.h"

int main(int argc, char *argv[])
{
    int fd = open("/dev/mouse", O_WRONLY);
    if(fd < 0){
        printf(1, "mousetest: failed to open /dev/mouse\n");
        exit();
    }

    printf(1, "mousetest: moving cursor to Terminal 1 titlebar at (320, 70)...\n");

    /* Cursor starts at (320, 240). Move up to y=70 (Terminal 1 title bar) */
    for(int i = 0; i < 17; i++){
        mouse_event_t ev;
        ev.dx = 0;
        ev.dy = -10;
        ev.buttons = 0;
        write(fd, &ev, sizeof(ev));
        sleep(2);
    }

    printf(1, "mousetest: clicking titlebar to start live drag...\n");
    /* Button down on titlebar */
    mouse_event_t ev_down = { 0, 0, 1 };
    write(fd, &ev_down, sizeof(ev_down));
    sleep(5);

    printf(1, "mousetest: dragging window live diagonally (+15, +10) x 10 steps...\n");
    /* Drag diagonally in real time */
    for(int i = 0; i < 10; i++){
        mouse_event_t ev;
        ev.dx = 15;
        ev.dy = 10;
        ev.buttons = 1;
        write(fd, &ev, sizeof(ev));
        sleep(3);
    }

    printf(1, "mousetest: releasing window...\n");
    /* Button up */
    mouse_event_t ev_up = { 0, 0, 0 };
    write(fd, &ev_up, sizeof(ev_up));
    sleep(5);

    printf(1, "mousetest: done!\n");
    close(fd);
    exit();
}
