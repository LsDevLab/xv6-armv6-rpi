#include "types.h"
#include "stat.h"
#include "user.h"
#include "fcntl.h"

#define MAX_WINDOWS 4
#define WIN_WIDTH   460
#define WIN_HEIGHT  320
#define TITLEBAR_HEIGHT 20

#define FB_WIDTH    1024
#define FB_HEIGHT   768
#define TASKBAR_HEIGHT 32
#define TASKBAR_Y   (FB_HEIGHT - TASKBAR_HEIGHT)

#define FB_DEVICE "/dev/fb"
#define KB_DEVICE "/dev/uart_keyboard"
#define MOUSE_DEVICE "/dev/mouse"

#define CHAR_W 8
#define CHAR_H 16
#define MAX_RECTS 32

/* Color palette (RGB565) */
#define COLOR_DESKTOP        0x0410  /* Classic Teal */
#define COLOR_TASKBAR        0xC618  /* Light Silver / Gray */
#define COLOR_TASKBAR_BORDER 0xFFFF  /* White 3D bevel */
#define COLOR_TITLE_ACTIVE   0x19B9  /* Deep Royal Blue */
#define COLOR_TITLE_INACTIVE 0x4A69  /* Slate Gray */
#define COLOR_WIN_BG         0x0000  /* Terminal Black */
#define COLOR_WIN_FG         0xFFFF  /* Crisp White */
#define COLOR_BORDER_ACTIVE  0xFFFF  /* White border */
#define COLOR_BORDER_INACT   0x7BEF  /* Gray border */
#define COLOR_CLOSE_RED      0xF800  /* Red [X] */

/* ==================================================
 * MOUSE CURSOR SPRITE (11 x 16 Classic Arrow)
 * ================================================== */
#define CURSOR_W 11
#define CURSOR_H 16

static const char *cursor_bitmap[16] = {
    "#..........",
    "##.........",
    "#W#........",
    "#WW#.......",
    "#WWW#......",
    "#WWWW#.....",
    "#WWWWW#....",
    "#WWWWWW#...",
    "#WWWWWWW#..",
    "#WWWW####..",
    "#WW#W#.....",
    "#W#..#W#...",
    "##...#W#...",
    "#.....#W#..",
    "......#W#..",
    ".......##.."
};

static int mouse_x = 320;
static int mouse_y = 240;
static int mouse_saved_x = 0;
static int mouse_saved_y = 0;
static int mouse_saved = 0;
static int mouse_buttons = 0;
static int mouse_dragging = 0;
static int drag_offset_x = 0;
static int drag_offset_y = 0;
static int mouse_mode = 0;
static int esc_state = 0;

static u16 mouse_under[CURSOR_W * CURSOR_H];
static u16 cursor_composite[CURSOR_W * CURSOR_H];

typedef struct {
    int x, y, w, h;
} rect_t;

typedef struct window {
    int win_id;
    int x, y;
    int width, height;
    char title[32];

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

/* Forward declarations */
void draw_desktop_background(int fb_fd);
void draw_taskbar(int fb_fd, int active_win_id, const char *status);
void blit_window_rects(int fb_fd, int idx);
void redraw_all(int fb_fd);
int get_focused_window_id(void);
void update_all_window_decorations(void);
void focus_window(int idx);
void focus_by_id(int target_id);
void start_process_in_window(char *name, char *title, int x, int y, int win_id);

/* ==================================================
 * MOUSE RENDERING (ZERO-FLICKER BACKGROUND RESTORE)
 * ================================================== */

void mouse_save(int fb_fd, int x, int y)
{
    fb_pixel_t req;
    req.x = x;
    req.y = y;
    req.w = CURSOR_W;
    req.h = CURSOR_H;
    req.buffer = mouse_under;
    req.stride = CURSOR_W;
    req.sx = 0;
    req.sy = 0;
    req.color = 0;
    req.ch = 0;
    read(fb_fd, &req, sizeof(req));
    mouse_saved_x = x;
    mouse_saved_y = y;
    mouse_saved = 1;
}

void mouse_restore(int fb_fd)
{
    if(!mouse_saved) return;
    fb_pixel_t req;
    req.x = mouse_saved_x;
    req.y = mouse_saved_y;
    req.w = CURSOR_W;
    req.h = CURSOR_H;
    req.buffer = mouse_under;
    req.stride = CURSOR_W;
    req.sx = 0;
    req.sy = 0;
    req.color = 0;
    req.ch = 0;
    write(fb_fd, &req, sizeof(req));
    mouse_saved = 0;
}

void mouse_render(int fb_fd, int x, int y)
{
    mouse_save(fb_fd, x, y);

    for(int i = 0; i < CURSOR_W * CURSOR_H; i++)
        cursor_composite[i] = mouse_under[i];

    for(int cy = 0; cy < CURSOR_H; cy++){
        for(int cx = 0; cx < CURSOR_W; cx++){
            char ch = cursor_bitmap[cy][cx];
            if(ch == '#')
                cursor_composite[cy * CURSOR_W + cx] = 0x0000;
            else if(ch == 'W')
                cursor_composite[cy * CURSOR_W + cx] = 0xFFFF;
        }
    }

    fb_pixel_t req;
    req.x = x;
    req.y = y;
    req.w = CURSOR_W;
    req.h = CURSOR_H;
    req.buffer = cursor_composite;
    req.stride = CURSOR_W;
    req.sx = 0;
    req.sy = 0;
    req.color = 0;
    req.ch = 0;
    write(fb_fd, &req, sizeof(req));
}

void refresh_screen(int fb_fd, const char *status)
{
    mouse_restore(fb_fd);
    draw_desktop_background(fb_fd);
    redraw_all(fb_fd);
    draw_taskbar(fb_fd, get_focused_window_id(), status);
    mouse_render(fb_fd, mouse_x, mouse_y);
}

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
 * FRAMEBUFFER PRIMITIVES
 * ================================================== */

void fb_draw_string(int fb_fd, int x, int y, const char *s, u16 color)
{
    while(*s){
        fb_pixel_t p;
        p.x = x;
        p.y = y;
        p.w = 0;
        p.h = 0;
        p.ch = *s;
        p.color = color;
        p.buffer = 0;
        p.stride = 0;
        p.sx = 0;
        p.sy = 0;
        write(fb_fd, &p, sizeof(p));
        x += CHAR_W;
        s++;
    }
}

void draw_desktop_background(int fb_fd)
{
    fb_pixel_t p;
    p.x = 0;
    p.y = 0;
    p.w = FB_WIDTH;
    p.h = TASKBAR_Y;
    p.color = COLOR_DESKTOP;
    p.ch = 0;
    p.buffer = 0;
    p.stride = 0;
    p.sx = 0;
    p.sy = 0;
    write(fb_fd, &p, sizeof(p));

    /* Top banner instructions */
    fb_draw_string(fb_fd, 90, 10, "ANTIGRAVITY OS -- Physical Mouse: Click QEMU window to grab | Terminal: Arrow keys move", 0xFFFF);
    fb_draw_string(fb_fd, 110, 28, "Mouse Controls: Click to Focus | Drag Titlebar to Move | Click [X] to Close | Tab: Cycle", 0xFFE0);
}

void draw_taskbar(int fb_fd, int active_win_id, const char *status)
{
    fb_pixel_t p;

    /* Taskbar background */
    p.x = 0;
    p.y = TASKBAR_Y;
    p.w = FB_WIDTH;
    p.h = TASKBAR_HEIGHT;
    p.color = COLOR_TASKBAR;
    p.ch = 0;
    p.buffer = 0;
    p.stride = 0;
    p.sx = 0;
    p.sy = 0;
    write(fb_fd, &p, sizeof(p));

    /* 3D bevel top line */
    p.x = 0;
    p.y = TASKBAR_Y;
    p.w = FB_WIDTH;
    p.h = 1;
    p.color = COLOR_TASKBAR_BORDER;
    write(fb_fd, &p, sizeof(p));

    /* Brand / Start button (clicking spawns new terminal if closed) */
    fb_pixel_t sbtn;
    sbtn.x = 6; sbtn.y = TASKBAR_Y + 4; sbtn.w = 150; sbtn.h = 24;
    sbtn.color = 0x39E7; sbtn.ch = 0; sbtn.buffer = 0; sbtn.stride = 0; sbtn.sx = 0; sbtn.sy = 0;
    write(fb_fd, &sbtn, sizeof(sbtn));
    fb_draw_string(fb_fd, 14, TASKBAR_Y + 8, "[ Antigravity OS ]", 0xFFFF);

    /* Window tabs with high-contrast active button */
    int has_win0 = 0, has_win1 = 0;
    for(int i = 0; i < num_windows; i++){
        if(windows[i].win_id == 0) has_win0 = 1;
        if(windows[i].win_id == 1) has_win1 = 1;
    }

    if(has_win0){
        fb_pixel_t btn;
        btn.x = 175; btn.y = TASKBAR_Y + 4; btn.w = 130; btn.h = 24;
        btn.color = (active_win_id == 0) ? COLOR_TITLE_ACTIVE : 0x7BEF;
        btn.ch = 0; btn.buffer = 0; btn.stride = 0; btn.sx = 0; btn.sy = 0;
        write(fb_fd, &btn, sizeof(btn));
        fb_draw_string(fb_fd, 185, TASKBAR_Y + 8, (active_win_id == 0) ? "* [1] Term 1 *" : "  [1] Term 1  ", (active_win_id == 0) ? 0xFFFF : 0x0000);
    }

    if(has_win1){
        fb_pixel_t btn;
        btn.x = 320; btn.y = TASKBAR_Y + 4; btn.w = 130; btn.h = 24;
        btn.color = (active_win_id == 1) ? COLOR_TITLE_ACTIVE : 0x7BEF;
        btn.ch = 0; btn.buffer = 0; btn.stride = 0; btn.sx = 0; btn.sy = 0;
        write(fb_fd, &btn, sizeof(btn));
        fb_draw_string(fb_fd, 330, TASKBAR_Y + 8, (active_win_id == 1) ? "* [2] Term 2 *" : "  [2] Term 2  ", (active_win_id == 1) ? 0xFFFF : 0x0000);
    }

    /* Live status indicator */
    if(status)
        fb_draw_string(fb_fd, 480, TASKBAR_Y + 8, status, 0x0000);
    else
        fb_draw_string(fb_fd, 480, TASKBAR_Y + 8, "Status: Ready", 0x4208);
}

/* ==================================================
 * WINDOW DECORATIONS & BUFFER DRAWING
 * ================================================== */

void window_draw_char_buf(u16 *buf, int stride, int x, int y, char c, u16 fg, u16 bg)
{
    if((u8)c > 127) return;
    u8 *glyph = font + ((u8)c << 4);
    for(int row = 0; row < 16; row++){
        u8 bits = glyph[row];
        for(int bit = 0; bit < 8; bit++){
            int px = x + bit;
            int py = y + row;
            if(px < stride){
                buf[py * stride + px] = (bits & (1 << bit)) ? fg : bg;
            }
        }
    }
}

void window_draw_text_buf(u16 *buf, int stride, int x, int y, const char *s, u16 fg, u16 bg)
{
    while(*s){
        window_draw_char_buf(buf, stride, x, y, *s, fg, bg);
        x += CHAR_W;
        s++;
    }
}

void draw_window_decorations(window_t *w, int focused)
{
    u16 border_color = focused ? COLOR_BORDER_ACTIVE : COLOR_BORDER_INACT;
    u16 tb_bg = focused ? COLOR_TITLE_ACTIVE : COLOR_TITLE_INACTIVE;
    u16 tb_fg = COLOR_WIN_FG;

    /* 1. Outer borders */
    for(int x = 0; x < w->width; x++){
        w->buffer[0 * w->width + x] = border_color;
        w->buffer[(w->height - 1) * w->width + x] = border_color;
    }
    for(int y = 0; y < w->height; y++){
        w->buffer[y * w->width + 0] = border_color;
        w->buffer[y * w->width + (w->width - 1)] = border_color;
    }

    /* 2. Title bar background */
    for(int y = 1; y < TITLEBAR_HEIGHT; y++){
        for(int x = 1; x < w->width - 1; x++){
            w->buffer[y * w->width + x] = tb_bg;
        }
    }

    /* 3. Title bar separator */
    for(int x = 1; x < w->width - 1; x++){
        w->buffer[TITLEBAR_HEIGHT * w->width + x] = border_color;
    }

    /* 4. Title text */
    window_draw_text_buf(w->buffer, w->width, 8, 2, w->title, tb_fg, tb_bg);

    /* 5. Close button [X] */
    int cx = w->width - 24;
    window_draw_char_buf(w->buffer, w->width, cx, 2, '[', tb_fg, tb_bg);
    window_draw_char_buf(w->buffer, w->width, cx + 8, 2, 'X', COLOR_CLOSE_RED, tb_bg);
    window_draw_char_buf(w->buffer, w->width, cx + 16, 2, ']', tb_fg, tb_bg);
}

void update_all_window_decorations(void)
{
    for(int i = 0; i < num_windows; i++){
        int is_focused = (i == num_windows - 1);
        draw_window_decorations(&windows[i], is_focused);
    }
}

/* ==================================================
 * SCANLINE FAST BLIT
 * ================================================== */

void blit_window_rects(int fb_fd, int idx)
{
    rect_t rects[MAX_RECTS];
    int n = compute_visible_rects(idx, rects);
    window_t *w = &windows[idx];

    for(int r = 0; r < n; r++){
        rect_t *rc = &rects[r];
        fb_pixel_t p;
        p.x = rc->x;
        p.y = rc->y;
        p.w = rc->w;
        p.h = rc->h;
        p.stride = w->width;
        p.sx = rc->x - w->x;
        p.sy = rc->y - w->y;
        p.buffer = w->buffer;
        p.color = 0;
        p.ch = 0;
        write(fb_fd, &p, sizeof(p));
    }
}

void redraw_all(int fb_fd)
{
    for(int i = 0; i < num_windows; i++)
        blit_window_rects(fb_fd, i);
}

/* ==================================================
 * TERMINAL TEXT ENGINE & SCROLLING
 * ================================================== */

void window_put_char(window_t *w, char c)
{
    int max_cols = (w->width - 12) / CHAR_W;
    int max_rows = (w->height - TITLEBAR_HEIGHT - 6) / CHAR_H;

    if(c == '\r') {
        w->cursor_x = 0;
        return;
    }

    if(c == '\n'){
        w->cursor_x = 0;
        w->cursor_y++;
        if(w->cursor_y >= max_rows) {
            int start_y = TITLEBAR_HEIGHT + 2;
            int total_h = (max_rows - 1) * CHAR_H;
            for(int y = start_y; y < start_y + total_h; y++){
                for(int x = 2; x < w->width - 2; x++){
                    w->buffer[y * w->width + x] = w->buffer[(y + CHAR_H) * w->width + x];
                }
            }
            for(int y = start_y + total_h; y < start_y + total_h + CHAR_H; y++){
                for(int x = 2; x < w->width - 2; x++){
                    w->buffer[y * w->width + x] = w->bg_color;
                }
            }
            w->cursor_y = max_rows - 1;
        }
        return;
    }

    if(c == '\b' || c == 127){
        if(w->cursor_x > 0){
            w->cursor_x--;
            int px = 4 + w->cursor_x * CHAR_W;
            int py = TITLEBAR_HEIGHT + 2 + w->cursor_y * CHAR_H;
            for(int row = 0; row < CHAR_H; row++){
                for(int col = 0; col < CHAR_W; col++){
                    w->buffer[(py + row) * w->width + (px + col)] = w->bg_color;
                }
            }
        }
        return;
    }

    if(w->cursor_x >= max_cols){
        w->cursor_x = 0;
        w->cursor_y++;
    }

    if(w->cursor_y >= max_rows){
        int start_y = TITLEBAR_HEIGHT + 2;
        int total_h = (max_rows - 1) * CHAR_H;
        for(int y = start_y; y < start_y + total_h; y++){
            for(int x = 2; x < w->width - 2; x++){
                w->buffer[y * w->width + x] = w->buffer[(y + CHAR_H) * w->width + x];
            }
        }
        for(int y = start_y + total_h; y < start_y + total_h + CHAR_H; y++){
            for(int x = 2; x < w->width - 2; x++){
                w->buffer[y * w->width + x] = w->bg_color;
            }
        }
        w->cursor_y = max_rows - 1;
    }

    u8 *glyph = font + ((u8)c << 4);
    int px = 4 + w->cursor_x * CHAR_W;
    int py = TITLEBAR_HEIGHT + 2 + w->cursor_y * CHAR_H;

    for(int row = 0; row < 16; row++){
        u8 bits = (row < 15) ? glyph[row] : 0;
        for(int bit = 0; bit < 8; bit++){
            int x = px + bit;
            int y = py + row;
            if(x < w->width - 2 && y < w->height - 2){
                w->buffer[y * w->width + x] = (bits & (1 << bit)) ? w->fg_color : w->bg_color;
            }
        }
    }
    w->cursor_x++;
}

/* ==================================================
 * PROCESS & WINDOW MANAGEMENT
 * ================================================== */

void start_process_in_window(char *name, char *title, int x, int y, int win_id)
{
    if(num_windows >= MAX_WINDOWS) return;

    window_t *w = &windows[num_windows];

    w->win_id = win_id;
    w->x = x;
    w->y = y;
    w->width = WIN_WIDTH;
    w->height = WIN_HEIGHT;
    w->bg_color = COLOR_WIN_BG;
    w->fg_color = COLOR_WIN_FG;
    w->cursor_x = 0;
    w->cursor_y = 0;
    strcpy(w->title, title);

    w->buffer = malloc(w->width * w->height * sizeof(u16));

    for(int i = 0; i < w->width * w->height; i++)
        w->buffer[i] = w->bg_color;

    pipe(w->pipefd);
    pipe(w->stdout_pipe);

    int pid = fork();
    if(pid == 0){
        close(0);
        dup(w->pipefd[0]);
        close(w->pipefd[0]);
        close(w->pipefd[1]);

        close(1);
        dup(w->stdout_pipe[1]);

        close(2);
        dup(w->stdout_pipe[1]);
        close(w->stdout_pipe[0]);
        close(w->stdout_pipe[1]);

        char *argv[] = {name, 0};
        exec(name, argv);
        exit();
    }

    close(w->pipefd[0]);
    close(w->stdout_pipe[1]);
    fcntl(w->stdout_pipe[0], F_SETFL, O_NONBLOCK);

    w->pid = pid;
    focused_pid = pid;
    num_windows++;

    update_all_window_decorations();
}

void focus_window(int idx)
{
    if(idx < 0 || idx >= num_windows)
        return;

    window_t tmp = windows[idx];
    for(int i = idx; i < num_windows - 1; i++)
        windows[i] = windows[i + 1];
    windows[num_windows - 1] = tmp;

    focused_pid = tmp.pid;
    update_all_window_decorations();
}

void focus_by_id(int target_id)
{
    for(int i = 0; i < num_windows; i++){
        if(windows[i].win_id == target_id){
            focus_window(i);
            return;
        }
    }
}

int get_focused_window_id(void)
{
    if(num_windows == 0) return -1;
    return windows[num_windows - 1].win_id;
}

void focus_next_window(void)
{
    if(num_windows <= 1) return;
    focus_window(0);
}

void close_window(int fb_fd, int idx)
{
    if(idx < 0 || idx >= num_windows)
        return;

    window_t *w = &windows[idx];
    if(w->pid > 0)
        kill(w->pid);

    close(w->pipefd[1]);
    close(w->stdout_pipe[0]);
    if(w->buffer)
        free(w->buffer);

    for(int i = idx; i < num_windows - 1; i++)
        windows[i] = windows[i + 1];
    num_windows--;

    if(num_windows > 0){
        focused_pid = windows[num_windows - 1].pid;
        update_all_window_decorations();
    } else {
        focused_pid = -1;
    }

    refresh_screen(fb_fd, "Mouse: Closed Window");
}

/* ==================================================
 * MOUSE EVENT DISPATCH & WINDOW DRAGGING
 * ================================================== */

void drag_window_live(int fb_fd, int old_x, int old_y, int new_x, int new_y)
{
    if(num_windows == 0) return;
    window_t *w = &windows[num_windows - 1];

    /* 1. Clear the old window bounding box with desktop color */
    fb_pixel_t fill;
    fill.x = old_x;
    fill.y = old_y;
    fill.w = w->width;
    fill.h = w->height;
    fill.color = COLOR_DESKTOP;
    fill.ch = 0;
    fill.buffer = 0;
    fill.stride = 0;
    fill.sx = 0;
    fill.sy = 0;
    write(fb_fd, &fill, sizeof(fill));

    /* If old window touched the top banner, redraw banner */
    if(old_y < 44){
        fb_draw_string(fb_fd, 90, 10, "ANTIGRAVITY OS -- Physical Mouse: Click QEMU window to grab | Terminal: Arrow keys move", 0xFFFF);
        fb_draw_string(fb_fd, 110, 28, "Mouse Controls: Click to Focus | Drag Titlebar to Move | Click [X] to Close | Tab: Cycle", 0xFFE0);
    }

    /* 2. Update window coordinates to new position */
    w->x = new_x;
    w->y = new_y;

    /* 3. Redraw all windows in proper Z-order (lower windows first, dragged window on top) */
    redraw_all(fb_fd);
}

void handle_mouse_move(int fb_fd, int nx, int ny)
{
    if(nx < 0) nx = 0;
    if(nx > FB_WIDTH - CURSOR_W) nx = FB_WIDTH - CURSOR_W;
    if(ny < 0) ny = 0;
    if(ny > FB_HEIGHT - CURSOR_H) ny = FB_HEIGHT - CURSOR_H;

    if(nx == mouse_x && ny == mouse_y)
        return;

    mouse_restore(fb_fd);
    mouse_x = nx;
    mouse_y = ny;

    if(mouse_dragging && num_windows > 0){
        window_t *w = &windows[num_windows - 1];
        int wx = mouse_x - drag_offset_x;
        int wy = mouse_y - drag_offset_y;

        if(wx < 0) wx = 0;
        if(wx + w->width > FB_WIDTH) wx = FB_WIDTH - w->width;
        if(wy < 0) wy = 0;
        if(wy + w->height > TASKBAR_Y) wy = TASKBAR_Y - w->height;

        if(wx != w->x || wy != w->y){
            drag_window_live(fb_fd, w->x, w->y, wx, wy);
        }
    }

    mouse_render(fb_fd, mouse_x, mouse_y);
}

void handle_mouse_down(int fb_fd, int mx, int my)
{
    mouse_buttons = 1;

    /* 1. Check Taskbar */
    if(my >= TASKBAR_Y){
        /* Brand / Start button -> Spawn new terminal if slots available */
        if(mx >= 0 && mx <= 160){
            if(num_windows < MAX_WINDOWS){
                int new_id = (num_windows == 0) ? 0 : (windows[0].win_id == 0 ? 1 : 0);
                int wx = 60 + num_windows * 100;
                int wy = 70 + num_windows * 60;
                start_process_in_window("sh", (new_id == 0) ? "Terminal 1 - /bin/sh" : "Terminal 2 - /bin/sh", wx, wy, new_id);
                refresh_screen(fb_fd, "Spawned New Terminal Window");
            }
            return;
        }

        /* Tab 1 */
        if(mx >= 175 && mx <= 305){
            focus_by_id(0);
            refresh_screen(fb_fd, "Mouse: Switched to Terminal 1");
            return;
        }

        /* Tab 2 */
        if(mx >= 320 && mx <= 450){
            focus_by_id(1);
            refresh_screen(fb_fd, "Mouse: Switched to Terminal 2");
            return;
        }
        return;
    }

    /* 2. Check Windows from top-most down to bottom */
    for(int i = num_windows - 1; i >= 0; i--){
        window_t *w = &windows[i];
        if(mx >= w->x && mx < w->x + w->width && my >= w->y && my < w->y + w->height){
            /* Hit Close Button [X] */
            int close_btn_x = w->x + w->width - 26;
            if(mx >= close_btn_x && my < w->y + TITLEBAR_HEIGHT){
                close_window(fb_fd, i);
                return;
            }

            /* Hit Title Bar -> Bring to Front & Initiate Drag */
            if(my < w->y + TITLEBAR_HEIGHT + 6){
                if(i != num_windows - 1){
                    focus_window(i);
                }
                mouse_dragging = 1;
                drag_offset_x = mx - windows[num_windows - 1].x;
                drag_offset_y = my - windows[num_windows - 1].y;
                refresh_screen(fb_fd, "Mouse: Dragging Window...");
                return;
            }

            /* Hit Client Area -> Bring to Front */
            if(i != num_windows - 1){
                focus_window(i);
                refresh_screen(fb_fd, "Mouse: Focused Window");
            }
            return;
        }
    }
}

void handle_mouse_up(int fb_fd)
{
    mouse_buttons = 0;
    if(mouse_dragging){
        mouse_dragging = 0;
        refresh_screen(fb_fd, "Mouse: Window Released");
    }
}

/* ==================================================
 * MAIN ENTRY POINT
 * ================================================== */

int main(void)
{
    int fb_fd = open(FB_DEVICE, O_RDWR);
    int kb_fd = open(KB_DEVICE, O_RDONLY | O_NONBLOCK);
    int mouse_fd = open(MOUSE_DEVICE, O_RDWR | O_NONBLOCK);

    /* 1. Start initial shells */
    start_process_in_window("sh", "Terminal 1 - /bin/sh", 50, 60, 0);
    start_process_in_window("sh", "Terminal 2 - /bin/sh", 480, 100, 1);

    /* Focus window 0 initially */
    focus_by_id(0);

    /* 2. Initial full screen paint with mouse cursor */
    refresh_screen(fb_fd, "Antigravity OS Ready (Mouse & Keyboard Active)");

    while(1){
        int activity = 0;

        /* A. Process Mouse Device Input (/dev/mouse) with Event Coalescing */
        if(mouse_fd >= 0){
            mouse_event_t m_ev;
            int total_dx = 0;
            int total_dy = 0;
            int had_mouse = 0;

            while(read(mouse_fd, &m_ev, sizeof(m_ev)) == sizeof(m_ev)){
                activity = 1;
                had_mouse = 1;

                int btn_down = (m_ev.buttons & 1) && !(mouse_buttons & 1);
                int btn_up   = !(m_ev.buttons & 1) && (mouse_buttons & 1);

                if(btn_down){
                    if(total_dx != 0 || total_dy != 0){
                        handle_mouse_move(fb_fd, mouse_x + total_dx, mouse_y + total_dy);
                        total_dx = 0;
                        total_dy = 0;
                    }
                    handle_mouse_down(fb_fd, mouse_x, mouse_y);
                }
                if(btn_up){
                    if(total_dx != 0 || total_dy != 0){
                        handle_mouse_move(fb_fd, mouse_x + total_dx, mouse_y + total_dy);
                        total_dx = 0;
                        total_dy = 0;
                    }
                    handle_mouse_up(fb_fd);
                }

                total_dx += m_ev.dx;
                total_dy += m_ev.dy;
            }

            if(had_mouse && (total_dx != 0 || total_dy != 0)){
                handle_mouse_move(fb_fd, mouse_x + total_dx, mouse_y + total_dy);
            }
        }

        /* B. Process Keyboard Input (/dev/uart_keyboard) */
        char c;
        int n_key = read(kb_fd, &c, 1);
        if(n_key == 1){
            activity = 1;

            /* 1. ANSI Escape sequences state machine (Arrow keys & Navigation) */
            if(esc_state == 0){
                if(c == 27){
                    esc_state = 1;
                    continue;
                }
            } else if(esc_state == 1){
                if(c == '[' || c == 'O'){
                    esc_state = 2; /* Accept both standard CSI '[' and SS3 'O' from macOS terminal */
                    continue;
                } else {
                    esc_state = 0;
                }
            } else if(esc_state == 2){
                if((c >= '0' && c <= '9') || c == ';'){
                    continue; /* Skip parameter bytes in sequences like \033[1;5A */
                }
                esc_state = 0;
                if(c == 'A'){ /* Up Arrow */
                    handle_mouse_move(fb_fd, mouse_x, mouse_y - 20);
                    continue;
                }
                if(c == 'B'){ /* Down Arrow */
                    handle_mouse_move(fb_fd, mouse_x, mouse_y + 20);
                    continue;
                }
                if(c == 'C'){ /* Right Arrow */
                    handle_mouse_move(fb_fd, mouse_x + 20, mouse_y);
                    continue;
                }
                if(c == 'D'){ /* Left Arrow */
                    handle_mouse_move(fb_fd, mouse_x - 20, mouse_y);
                    continue;
                }
                if(c == 'H' || c == '~'){ /* Home key -> Left Click */
                    handle_mouse_down(fb_fd, mouse_x, mouse_y);
                    handle_mouse_up(fb_fd);
                    continue;
                }
                if(c == 'F'){ /* End key -> Drag Toggle */
                    if(mouse_dragging){
                        handle_mouse_up(fb_fd);
                    } else {
                        handle_mouse_down(fb_fd, mouse_x, mouse_y);
                    }
                    continue;
                }
                continue;
            }

            /* 2. Mouse Mode toggle (backtick '`') */
            if(c == '`'){
                mouse_mode = !mouse_mode;
                if(mouse_mode){
                    refresh_screen(fb_fd, "[MOUSE MODE: WASD move, SPACE click, G grab, ` exit]");
                } else {
                    if(mouse_dragging) handle_mouse_up(fb_fd);
                    refresh_screen(fb_fd, "[TYPING MODE: Type in shell, Arrow keys move]");
                }
                continue;
            }

            /* 3. Handle Mouse Mode controls */
            if(mouse_mode){
                if(c == 'w' || c == 'W'){ handle_mouse_move(fb_fd, mouse_x, mouse_y - 18); continue; }
                if(c == 's' || c == 'S'){ handle_mouse_move(fb_fd, mouse_x, mouse_y + 18); continue; }
                if(c == 'a' || c == 'A'){ handle_mouse_move(fb_fd, mouse_x - 18, mouse_y); continue; }
                if(c == 'd' || c == 'D'){ handle_mouse_move(fb_fd, mouse_x + 18, mouse_y); continue; }
                if(c == ' '){
                    handle_mouse_down(fb_fd, mouse_x, mouse_y);
                    handle_mouse_up(fb_fd);
                    continue;
                }
                if(c == 'g' || c == 'G'){
                    if(mouse_dragging){
                        handle_mouse_up(fb_fd);
                    } else {
                        handle_mouse_down(fb_fd, mouse_x, mouse_y);
                    }
                    continue;
                }
            }

            /* 4. Switch focus hotkeys: Tab, '1', '2' */
            if(c == '\t'){
                focus_next_window();
                refresh_screen(fb_fd, "Key: Tab -> Switched Focus");
                continue;
            }
            if(c == '1'){
                focus_by_id(0);
                refresh_screen(fb_fd, "Key: '1' -> Focused Terminal 1");
                continue;
            }
            if(c == '2'){
                focus_by_id(1);
                refresh_screen(fb_fd, "Key: '2' -> Focused Terminal 2");
                continue;
            }

            /* 5. Forward keystroke to focused window AND echo locally */
            for(int i = 0; i < num_windows; i++){
                if(windows[i].pid == focused_pid){
                    write(windows[i].pipefd[1], &c, 1);

                    if(c == '\r' || c == '\n'){
                        window_put_char(&windows[i], '\n');
                    } else if(c == '\b' || c == 127){
                        window_put_char(&windows[i], '\b');
                    } else if(c >= 32 && c <= 126){
                        window_put_char(&windows[i], c);
                    }

                    mouse_restore(fb_fd);
                    blit_window_rects(fb_fd, i);

                    char sbuf[32];
                    sbuf[0] = 'K'; sbuf[1] = 'e'; sbuf[2] = 'y'; sbuf[3] = ':'; sbuf[4] = ' ';
                    if(c >= 32 && c <= 126){
                        sbuf[5] = '\''; sbuf[6] = c; sbuf[7] = '\''; sbuf[8] = 0;
                    } else if(c == '\r' || c == '\n'){
                        sbuf[5] = '<'; sbuf[6] = 'E'; sbuf[7] = 'n'; sbuf[8] = 't'; sbuf[9] = 'e'; sbuf[10] = 'r'; sbuf[11] = '>'; sbuf[12] = 0;
                    } else {
                        sbuf[5] = '?'; sbuf[6] = 0;
                    }
                    draw_taskbar(fb_fd, get_focused_window_id(), sbuf);
                    mouse_render(fb_fd, mouse_x, mouse_y);
                    break;
                }
            }
        }

        /* C. Read output from child processes */
        for(int i = 0; i < num_windows; i++){
            char buf[64];
            int n = read(windows[i].stdout_pipe[0], buf, sizeof(buf));
            if(n > 0){
                activity = 1;
                for(int j = 0; j < n; j++)
                    window_put_char(&windows[i], buf[j]);

                mouse_restore(fb_fd);
                blit_window_rects(fb_fd, i);
                mouse_render(fb_fd, mouse_x, mouse_y);
            }
        }

        /* D. Sleep briefly if idle to yield CPU */
        if(!activity){
            sleep(1);
        }
    }
}
