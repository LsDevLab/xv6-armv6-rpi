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
#define MAX_RECTS 32

typedef struct {
    int x, y, w, h;
} rect_t;

typedef struct window {
    int x, y;
    int width, height;

    int pipefd[2];
    int stdout_pipe[2];
    int pid;

    u16 bg_color;
    u16 fg_color;

    u16 *buffer;
    int cursor_x;
    int cursor_y;
} window_t;

window_t windows[MAX_WINDOWS];
int num_windows = 0;
int focused_pid = -1;

extern u8 font[];

/* ==================================================
 * RECTANGLE CLIPPING
 * ================================================== */

int rect_intersect(rect_t *a, rect_t *b, rect_t *out)
{
    int x1 = a->x > b->x ? a->x : b->x;
    int y1 = a->y > b->y ? a->y : b->y;
    int x2 = (a->x + a->w < b->x + b->w) ? a->x + a->w : b->x + b->w;
    int y2 = (a->y + a->h < b->y + b->h) ? a->y + a->h : b->y + b->h;

    if(x1 >= x2 || y1 >= y2)
        return 0;

    out->x = x1;
    out->y = y1;
    out->w = x2 - x1;
    out->h = y2 - y1;
    return 1;
}

int rect_subtract(rect_t *r, rect_t *o, rect_t out[4])
{
    rect_t i;
    if(!rect_intersect(r, o, &i)){
        out[0] = *r;
        return 1;
    }

    int n = 0;

    if(i.y > r->y)
        out[n++] = (rect_t){ r->x, r->y, r->w, i.y - r->y };

    if(i.y + i.h < r->y + r->h)
        out[n++] = (rect_t){
            r->x,
            i.y + i.h,
            r->w,
            (r->y + r->h) - (i.y + i.h)
        };

    if(i.x > r->x)
        out[n++] = (rect_t){
            r->x,
            i.y,
            i.x - r->x,
            i.h
        };

    if(i.x + i.w < r->x + r->w)
        out[n++] = (rect_t){
            i.x + i.w,
            i.y,
            (r->x + r->w) - (i.x + i.w),
            i.h
        };

    return n;
}

int compute_visible_rects(int idx, rect_t *out)
{
    rect_t tmp1[MAX_RECTS], tmp2[MAX_RECTS];
    int cur = 1;

    window_t *w = &windows[idx];
    tmp1[0] = (rect_t){ w->x, w->y, w->width, w->height };

    for(int i = idx + 1; i < num_windows; i++){
        window_t *o = &windows[i];
        rect_t orect = { o->x, o->y, o->width, o->height };

        int next = 0;
        for(int r = 0; r < cur; r++){
            rect_t pieces[4];
            int n = rect_subtract(&tmp1[r], &orect, pieces);
            for(int k = 0; k < n; k++)
                tmp2[next++] = pieces[k];
        }

        cur = next;
        for(int k = 0; k < cur; k++)
            tmp1[k] = tmp2[k];

        if(cur == 0)
            break;
    }

    for(int i = 0; i < cur; i++)
        out[i] = tmp1[i];

    return cur;
}

/* ==================================================
 * FRAMEBUFFER
 * ================================================== */

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

#define FOCUSED_BORDER_COLOR 0xFFE0   /* yellow in RGB565 */
#define NORMAL_BORDER_COLOR 0xFFFF   /* white in RGB565 */

void draw_window_border(window_t *w, u16 color)
{

    /* top + bottom border */
    for(int x = 0; x < w->width; x++){
        w->buffer[0 * w->width + x] =
           color;
        w->buffer[(w->height - 1) * w->width + x] =
            color;
    }

    /* left + right border */
    for(int y = 0; y < w->height; y++){
        w->buffer[y * w->width + 0] =
            color;
        w->buffer[y * w->width + (w->width - 1)] =
            color;
    }
}

void blit_window_rects(int fb_fd, int idx)
{
    rect_t rects[MAX_RECTS];
    int n = compute_visible_rects(idx, rects);
    window_t *w = &windows[idx];

    for(int r=0;r<n;r++){
        rect_t *rc = &rects[r];
        for(int y=0;y<rc->h;y++){
            for(int x=0;x<rc->w;x++){
                int wx = rc->x - w->x + x;
                int wy = rc->y - w->y + y;
                u16 col = w->buffer[wy * w->width + wx];
                fb_draw_pixel(fb_fd, rc->x + x, rc->y + y, col);
            }
        }
    }
}

void redraw_all(int fb_fd)
{
    for(int i=0;i<num_windows;i++)
        blit_window_rects(fb_fd, i);
}

/* ==================================================
 * TEXT RENDERING
 * ================================================== */

void window_put_char(window_t *w, char c)
{
    if(c == '\n'){
        w->cursor_x = 0;
        w->cursor_y++;
        return;
    }

    if((w->cursor_x+1)*CHAR_W > w->width){
        w->cursor_x = 0;
        w->cursor_y++;
    }
    if((w->cursor_y+1)*CHAR_H > w->height)
        w->cursor_y = 0;

    u8 *glyph = font + ((u8)c << 4);

    int px = w->cursor_x * CHAR_W;
    int py = w->cursor_y * CHAR_H;

    for(int row=0;row<15;row++){
        u8 bits = glyph[row];
        for(int bit=0;bit<8;bit++){
            if(bits & (1<<bit)){
                int x = px + bit;
                int y = py + row;
                if(x < w->width && y < w->height)
                    w->buffer[y*w->width + x] = w->fg_color;
            }
        }
    }
    w->cursor_x++;
}

/* ==================================================
 * PROCESS / IO
 * ================================================== */

void draw_window_borders_according_focus()
{
    for(int i=0;i<num_windows;i++){
        window_t *w = &windows[i];
        if(i == num_windows - 1)
            draw_window_border(w, FOCUSED_BORDER_COLOR);
        else
            draw_window_border(w, NORMAL_BORDER_COLOR);
    }
}

void start_process_in_window(char *name, int x, int y)
{
    window_t *w = &windows[num_windows];

    w->x=x; w->y=y;
    w->width=WIN_WIDTH; w->height=WIN_HEIGHT;
    w->bg_color=0x001F;
    w->fg_color=0xFFFF;
    w->cursor_x=w->cursor_y=0;

    w->buffer = malloc(w->width*w->height*sizeof(u16));
    for(int i=0;i<w->width*w->height;i++)
        w->buffer[i]=w->bg_color;

    draw_window_borders_according_focus();

    pipe(w->pipefd);
    pipe(w->stdout_pipe);

    int pid = fork();
    if(pid==0){
        close(w->pipefd[1]);
        dup(w->pipefd[0]);
        close(w->pipefd[0]);

        close(w->stdout_pipe[0]);
        dup(w->stdout_pipe[1]);
        dup(w->stdout_pipe[1]);
        close(w->stdout_pipe[1]);

        char *argv[]={name,0};
        exec(name,argv);
        exit();
    }

    close(w->pipefd[0]);
    close(w->stdout_pipe[1]);

    w->pid=pid;
    focused_pid=pid;
    num_windows++;
}

void focus_window(int idx)
{
    window_t tmp = windows[idx];
    for(int i=idx;i<num_windows-1;i++)
        windows[i]=windows[i+1];
    windows[num_windows-1]=tmp;
    focused_pid=tmp.pid;
}

/* ==================================================
 * MAIN LOOP
 * ================================================== */

int main(void)
{
    int fb_fd = open(FB_DEVICE,O_WRONLY);
    int kb_fd = open(KB_DEVICE,O_RDONLY);

    start_process_in_window("sh",50,50);
    start_process_in_window("sh",500,50);

    redraw_all(fb_fd);

    while(1){
        char c;
        if(read(kb_fd,&c,1)==1){
            if(c=='1'){ focus_window(0); redraw_all(fb_fd); continue; }
            if(c=='2'){ focus_window(1); redraw_all(fb_fd); continue; }

            for(int i=0;i<num_windows;i++)
                if(windows[i].pid==focused_pid)
                    write(windows[i].pipefd[1],&c,1);
        }

        for(int i=0;i<num_windows;i++){
            char buf[64];
            int n = read(windows[i].stdout_pipe[0],buf,sizeof(buf));
            if(n>0){
                for(int j=0;j<n;j++)
                    window_put_char(&windows[i],buf[j]);
                blit_window_rects(fb_fd,i);
            }
        }
    }
}
