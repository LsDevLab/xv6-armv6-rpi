#include "types.h"
#include "stat.h"
#include "user.h"
#include "fcntl.h"

#define MAX_WINDOWS 2
#define WIN_WIDTH   400
#define WIN_HEIGHT  300

#define FB_DEVICE "/dev/fb"
#define KB_DEVICE "/dev/uart_keyboard"

#define CHAR_W 8
#define CHAR_H 16

typedef struct window {
    int x, y;
    int width, height;

    int pipefd[2];          // stdin
    int stdout_pipe[2];     // stdout/stderr
    int pid;

    u16 bg_color;
    u16 fg_color;

    u16 *buffer;            // pixel buffer
    int cursor_x;
    int cursor_y;
} window_t;

window_t windows[MAX_WINDOWS];
int num_windows = 0;
int focused_pid = -1;

extern u8 font[];

/* -------------------------------------------------- */
/*                  VISIBILITY LOGIC                  */
/* -------------------------------------------------- */

int pixel_visible(int win_idx, int x, int y)
{
    int sx = windows[win_idx].x + x;
    int sy = windows[win_idx].y + y;

    for(int i = win_idx + 1; i < num_windows; i++){
        window_t *w = &windows[i];
        if(sx >= w->x && sx < w->x + w->width &&
           sy >= w->y && sy < w->y + w->height)
            return 0;
    }
    return 1;
}

/* -------------------------------------------------- */
/*                 FRAMEBUFFER OUTPUT                 */
/* -------------------------------------------------- */

void fb_draw_pixel(int fb_fd, int x, int y, u16 color)
{
    fb_pixel_t p;
    p.x = x;
    p.y = y;
    p.color = color;
    p.ch = 0;
    p.w = p.h = 0;
    p.buffer = 0;
    write(fb_fd, &p, sizeof(p));
}

void draw_window_border(int fb_fd, int idx)
{
    window_t *w = &windows[idx];
    u16 col = (w->pid == focused_pid) ? 0xFFFF : 0x0000;

    for(int i = 0; i < w->width; i++){
        if(pixel_visible(idx, i, 0))
            fb_draw_pixel(fb_fd, w->x + i, w->y, col);
        if(pixel_visible(idx, i, w->height - 1))
            fb_draw_pixel(fb_fd, w->x + i, w->y + w->height - 1, col);
    }

    for(int i = 0; i < w->height; i++){
        if(pixel_visible(idx, 0, i))
            fb_draw_pixel(fb_fd, w->x, w->y + i, col);
        if(pixel_visible(idx, w->width - 1, i))
            fb_draw_pixel(fb_fd, w->x + w->width - 1, w->y + i, col);
    }
}

void blit_window_visible(int fb_fd, int idx)
{
    window_t *w = &windows[idx];

    for(int y = 0; y < w->height; y++){
        for(int x = 0; x < w->width; x++){
            if(!pixel_visible(idx, x, y))
                continue;

            u16 col = w->buffer[y * w->width + x];
            fb_draw_pixel(fb_fd, w->x + x, w->y + y, col);
        }
    }

    draw_window_border(fb_fd, idx);
}

void redraw_all(int fb_fd)
{
    for(int i = 0; i < num_windows; i++)
        blit_window_visible(fb_fd, i);
}

/* -------------------------------------------------- */
/*               WINDOW BUFFER WRITING                */
/* -------------------------------------------------- */

void window_put_char(window_t *w, char c, int *cx, int *cy)
{
    if(c == '\n'){
        w->cursor_x = 0;
        w->cursor_y++;
        return;
    }

    if((w->cursor_x + 1) * CHAR_W > w->width){
        w->cursor_x = 0;
        w->cursor_y++;
    }

    if((w->cursor_y + 1) * CHAR_H > w->height){
        w->cursor_y = 0;
    }

    if((unsigned char)c > 127)
        return;

    *cx = w->cursor_x;
    *cy = w->cursor_y;

    u8 *glyph = font + ((u8)c << 4);

    int px = w->cursor_x * CHAR_W;
    int py = w->cursor_y * CHAR_H;

    for(int row = 0; row < 15; row++){
        u8 bits = glyph[row];
        for(int bit = 0; bit < 8; bit++){
            if(bits & (1 << bit)){
                int x = px + bit;
                int y = py + row;
                if(x < w->width && y < w->height)
                    w->buffer[y * w->width + x] = w->fg_color;
            }
        }
    }

    w->cursor_x++;
}

void draw_char_visible(int fb_fd, int idx, int cx, int cy)
{
    window_t *w = &windows[idx];

    for(int row = 0; row < CHAR_H; row++){
        for(int col = 0; col < CHAR_W; col++){
            int x = cx * CHAR_W + col;
            int y = cy * CHAR_H + row;

            if(x >= w->width || y >= w->height)
                continue;

            if(!pixel_visible(idx, x, y))
                continue;

            u16 color = w->buffer[y * w->width + x];
            fb_draw_pixel(fb_fd, w->x + x, w->y + y, color);
        }
    }
}

/* -------------------------------------------------- */
/*                PROCESS / WINDOW SETUP              */
/* -------------------------------------------------- */

void start_process_in_window(char *name, int x, int y)
{
    window_t *w = &windows[num_windows];

    w->x = x;
    w->y = y;
    w->width  = WIN_WIDTH;
    w->height = WIN_HEIGHT;
    w->bg_color = 0x001F;
    w->fg_color = 0xFFFF;
    w->cursor_x = 0;
    w->cursor_y = 0;

    w->buffer = malloc(w->width * w->height * sizeof(u16));
    for(int i = 0; i < w->width * w->height; i++)
        w->buffer[i] = w->bg_color;

    pipe(w->pipefd);
    pipe(w->stdout_pipe);

    int pid = fork();
    if(pid == 0){
        close(w->pipefd[1]);
        dup(w->pipefd[0]);
        close(w->pipefd[0]);

        close(w->stdout_pipe[0]);
        dup(w->stdout_pipe[1]);
        dup(w->stdout_pipe[1]);
        close(w->stdout_pipe[1]);

        char *argv[] = { name, 0 };
        exec(name, argv);
        exit();
    }

    close(w->pipefd[0]);
    close(w->stdout_pipe[1]);

    w->pid = pid;
    focused_pid = pid;
    num_windows++;
}

/* -------------------------------------------------- */
/*                  INPUT / OUTPUT                    */
/* -------------------------------------------------- */

void focus_window(int idx)
{
    window_t tmp = windows[idx];
    for(int i = idx; i < num_windows - 1; i++)
        windows[i] = windows[i + 1];
    windows[num_windows - 1] = tmp;
    focused_pid = tmp.pid;
}

void handle_keyboard(int kb_fd, int fb_fd)
{
    char c;
    if(read(kb_fd, &c, 1) != 1)
        return;

    if(c == '1'){ focus_window(0); redraw_all(fb_fd); return; }
    if(c == '2'){ focus_window(1); redraw_all(fb_fd); return; }

    for(int i = 0; i < num_windows; i++){
        if(windows[i].pid == focused_pid){
            write(windows[i].pipefd[1], &c, 1);
            break;
        }
    }
}

void handle_stdout(int fb_fd)
{
    char buf[64];

    for(int i = 0; i < num_windows; i++){
        int n = read(windows[i].stdout_pipe[0], buf, sizeof(buf));
        if(n <= 0)
            continue;

        for(int j = 0; j < n; j++){
            int cx = 0, cy = 0;
            window_put_char(&windows[i], buf[j], &cx, &cy);
            draw_char_visible(fb_fd, i, cx, cy);
        }
    }
}

/* -------------------------------------------------- */
/*                        MAIN                        */
/* -------------------------------------------------- */

int main(void)
{
    int fb_fd = open(FB_DEVICE, O_WRONLY);
    int kb_fd = open(KB_DEVICE, O_RDONLY);

    start_process_in_window("sh", 50, 50);
    start_process_in_window("sh", 500, 50);

    redraw_all(fb_fd);

    while(1){
        handle_keyboard(kb_fd, fb_fd);
        handle_stdout(fb_fd);
    }
}
