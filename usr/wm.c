#include "types.h"
#include "stat.h"
#include "user.h"
#include "file.h"
#include "fcntl.h"

#define MAX_WINDOWS 2
#define WIN_WIDTH 400
#define WIN_HEIGHT 300

#define FB_DEVICE "/dev/fb"
#define KB_DEVICE "/dev/uart_keyboard"

typedef struct window {
    int x, y;
    int width, height;
    int pipefd[2];       // stdin pipe for child
    int stdout_pipe[2];       // stdout pipe for child
    int pid;             // child process pid
    u16 bg_color;
    u16 fg_color;
} window_t;

int focused_window = -1;

window_t windows[MAX_WINDOWS];
int num_windows = 0;
int active_window = 0;


void draw_rect(int fb_fd, uint x, uint y, uint w, uint h, u16 color) {
    fb_pixel_t pix;
    pix.color = color;
    pix.ch = 0;

    for(uint row = 0; row < h; row++){
        for(uint col = 0; col < w; col++){
            pix.x = x + col;
            pix.y = y + row;
            write(fb_fd, &pix, sizeof(pix));
        }
    }
}

void draw_window_border(int fb_fd, window_t *win, int focused) {
    int color = focused ? 0xFFFF : 0x0000; // white border if focused
    draw_rect(fb_fd, win->x, win->y, win->width, 2, color);          // top border
    draw_rect(fb_fd, win->x, win->y + win->height-2, win->width, 2, color); // bottom
    draw_rect(fb_fd, win->x, win->y, 2, win->height, color);         // left
    draw_rect(fb_fd, win->x + win->width-2, win->y, 2, win->height, color); // right
}

// draw a filled rectangle in the window
void draw_window(int fb_fd, window_t *win) {
    fb_pixel_t pix;
    for(int y = 0; y < win->height; y++) {
        for(int x = 0; x < win->width; x++) {
            pix.x = win->x + x;
            pix.y = win->y + y;
            pix.color = win->bg_color;
            write(fb_fd, &pix, sizeof(pix));
        }
    }
    focused_window = (*win).pid;
    draw_window_border(fb_fd, win, focused_window);

}

// start a user process (shell) in a window
void start_process_in_window( char *name, int x, int y, int fb_fd) {
    if(num_windows >= MAX_WINDOWS) return;

    window_t *win = &windows[num_windows];
    win->x = x; win->y = y;
    win->width = WIN_WIDTH; win->height = WIN_HEIGHT;
    win->bg_color = 0xff00;   // red
    win->fg_color = 0xffff;   // white

    draw_window(fb_fd, win);

    if(pipe(win->pipefd) < 0){
        //printf(1, "pipe failed\n");
        return;
    }

    int outpipe[2];
    if(pipe(outpipe) < 0){
        //cprintf("stdout pipe failed\n");
        return;
    }
    win->stdout_pipe[0] = outpipe[0]; // read end (WM reads)
    win->stdout_pipe[1] = outpipe[1]; // write end (shell writes)

    int pid = fork();
    if(pid == 0){
        // child: redirect stdin from pipe
        close(win->pipefd[1]);
        dup(win->pipefd[0]);  // stdin = pipe read end
        close(win->pipefd[0]);

        close(win->stdout_pipe[0]);     // close read end (WM)
        dup(win->stdout_pipe[1]);       // stdout = pipe write end
        dup(win->stdout_pipe[1]);       // stderr = pipe write end
        close(win->stdout_pipe[1]);

        char *argv[] = {(char*)name, 0};
        exec(name, argv);
        exit();
    }

    // parent: keep write end for input forwarding
    close(win->pipefd[0]);
    win->pid = pid;
    num_windows++;
}



// forward keyboard input to active window
void handle_keyboard(int kb_fd, int fb_fd) {
    char buf[16];
    int n = read(kb_fd, buf, sizeof(buf));
    if(n <= 0) return;

    for(int i = 0; i < n; i++){
        char c = buf[i];

        // switch focus on '1' or '2'
        if(c == '1' && focused_window != windows[0].pid){
            focused_window = windows[0].pid;
            draw_window_border(fb_fd, &windows[0], 1);
            draw_window_border(fb_fd, &windows[1], 0);
            continue;
        }
        if(c == '2' && focused_window != windows[1].pid){
            focused_window = windows[1].pid;
            draw_window_border(fb_fd, &windows[0], 0);
            draw_window_border(fb_fd, &windows[1], 1);
            continue;
        }

        // send input to focused window only
        for(int w = 0; w < num_windows; w++){
            if(windows[w].pid == focused_window){
                write(windows[w].pipefd[1], &c, 1);
                break;
            }
        }
    }
}


void draw_char_in_window(int fb_fd, window_t *win, char c){
    static int cursor_x[MAX_WINDOWS] = {0};
    static int cursor_y[MAX_WINDOWS] = {0};
    int x = win->x + cursor_x[win - windows] * 8;
    int y = win->y + cursor_y[win - windows] * 16;

    fb_pixel_t pix;
    // draw character bitmap (like your font)
    for(int row = 0; row < 16; row++){
        for(int col = 0; col < 8; col++){
            pix.x = x + col;
            pix.y = y + row;
            pix.ch = c;
            pix.color = win->fg_color;
            write(fb_fd, &pix, sizeof(pix));
        }
    }

    cursor_x[win - windows]++;
    if(cursor_x[win - windows] >= win->width / 8){
        cursor_x[win - windows] = 0;
        cursor_y[win - windows]++;
        if(cursor_y[win - windows] >= win->height / 16){
            cursor_y[win - windows] = 0; // simple scrolling can be added
        }
    }
}

void wm_draw_shell_output(int fb_fd) {
    char buf[64];
    for(int i = 0; i < num_windows; i++) {
        window_t *win = &windows[i];
        int n = read(win->stdout_pipe[0], buf, sizeof(buf));
        if(n > 0){
            for(int j = 0; j < n; j++){
                char c = buf[j];
                draw_char_in_window(fb_fd, win, c);
            }
        }
    }
}

int main(void) {
    int fb_fd = open(FB_DEVICE, O_WRONLY);
    if(fb_fd < 0){
        //printf(1, "cannot open framebuffer\n");
        exit();
    }

    int kb_fd = open(KB_DEVICE, O_RDONLY);
    if(kb_fd < 0){
        //printf(1, "cannot open keyboard device\n");
        exit();
    }

    // start two shells in fixed positions
    start_process_in_window("sh", 50, 50, fb_fd);
    start_process_in_window("sh", 500, 50, fb_fd);

    // main loop
    while(1){
        handle_keyboard(kb_fd, fb_fd);
        wm_draw_shell_output(fb_fd);   // multiplex stdout
        // crude delay
        for(volatile int i=0;i<100000;i++);
    }

    exit();
}
