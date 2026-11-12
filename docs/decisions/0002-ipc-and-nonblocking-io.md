# ADR 0002: Window Manager IPC & Non-Blocking I/O Architecture

## Status
Accepted

## Context & Root Cause Analysis of "wm ... not working"
In commit `85798b8` ("wm with layered windows efficient. not working"), the user implemented an event loop in `usr/wm.c`:
```c
while(1) {
    char c;
    if (read(kb_fd, &c, 1) == 1) { ... }

    for (int i = 0; i < num_windows; i++) {
        char buf[64];
        int n = read(windows[i].stdout_pipe[0], buf, sizeof(buf));
        ...
    }
}
```
Two fundamental blocking behaviors in xv6 cause this loop to hang:
1. **Blocking Pipe Reads (`piperead`)**: In `pipe.c`, when a pipe is empty (`p->nread == p->nwrite`), `piperead()` puts the process to sleep:
   ```c
   while (p->nread == p->nwrite && p->writeopen) {
       sleep(&p->nread, &p->lock);
   }
   ```
   If window 0 has no output, `read(windows[0].stdout_pipe[0])` blocks the entire `wm` process. Window 1 and the keyboard are never processed.
2. **Blocking Line-Buffered Keyboard Reads (`uartkbdread`)**:
   In `uart_keyboard.c`, `uartkbdintr` only wakes up waiting readers when `c == '\n'` or `EOF`:
   ```c
   if (c == '\n' || c == C('D') || input.e == input.r + INPUT_BUF) {
       input.w = input.e;
       wakeup(&input.r);
   }
   ```
   Single keystrokes (like `'1'` or `'2'` for window focus) do not wake up the reader until Enter is pressed, and `uartkbdread` blocks if no newline has arrived.

## Decision
To support an interactive Window Server that multiplexes input devices and multiple client processes:

1. **Non-Blocking I/O or `fcntl(O_NONBLOCK)`**:
   - Add non-blocking support to `struct file`, `pipe`, and device drivers (`devsw`).
   - If an operation would block and non-blocking mode is enabled, return `0` or `-EAGAIN` immediately.
2. **Raw Character Mode for Input Devices**:
   - For `/dev/uart_keyboard` (and later `/dev/kbd` and `/dev/mouse`), allow raw mode where every incoming character or packet immediately triggers `wakeup()` and is returned without requiring a newline delimiter.
3. **Dedicated Window Event IPC**:
   - Rather than repurposing standard Unix streams (`stdin`/`stdout` pipes) for graphical applications, GUI apps (`writer`, `drawer`, `calc`, `browser`) will communicate with the Window Server via structured message queues or a dedicated IPC channel.
   - Standard shell processes (`sh`) running in terminal windows will use pseudo-terminal / non-blocking pipes managed by terminal emulator windows.

## Consequences
- The Window Server main loop never blocks on any single client or input channel.
- Smooth framerates and immediate response to keystrokes and mouse movements.

## Implementation Details
1. **Syscall `fcntl`**:
   - Added system call `SYS_fcntl` (number 22) supporting `F_GETFL` and `F_SETFL` with flag `O_NONBLOCK` (`0x004`).
   - Stored `nonblock` flag directly in `struct file` upon `open()` or `fcntl(fd, F_SETFL, O_NONBLOCK)`.
2. **Non-blocking Pipe Operations**:
   - `piperead(p, addr, n, nonblock)`: Returns `0` immediately when `p->nread == p->nwrite && p->writeopen && nonblock`.
   - `pipewrite(p, addr, n, nonblock)`: Returns written count immediately without sleeping when the pipe buffer is full.
3. **Raw Mode & Non-blocking Keyboard**:
   - `uartkbdintr()`: Delivers characters immediately on receipt (`input.w = input.e; wakeup(&input.r);`) without waiting for Enter.
   - `uartkbdread(ip, dst, n, nonblock)`: Returns `0` immediately if no characters are available and `nonblock` is set. Also drains all available characters immediately in raw mode.
   - Added `uartkbdwrite()` to allow bidirectional console logging through `/dev/uart_keyboard`.
