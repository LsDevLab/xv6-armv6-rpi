struct stat;

// system calls
int fork(void);
int exit(void) __attribute__((noreturn));
int wait(void);
int pipe(int*);
int write(int, void*, int);
int read(int, void*, int);
int close(int);
int kill(int);
int exec(char*, char**);
int open(char*, int);
int mknod(char*, short, short);
int unlink(char*);
int fstat(int fd, struct stat*);
int link(char*, char*);
int mkdir(char*);
int chdir(char*);
int dup(int);
int getpid(void);
char* sbrk(int);
int sleep(int);
int uptime(void);
int fcntl(int, int, ...);

// ulib.c
int stat(char*, struct stat*);
char* strcpy(char*, char*);
void *memmove(void*, void*, int);
void* memcpy(void*, const void*, uint);
char* strchr(const char*, char c);
int strcmp(const char*, const char*);
void printf(int, char*, ...);
char* gets(char*, int max);
uint strlen(char*);
void* memset(void*, int, uint);
void* malloc(uint);
void free(void*);
int atoi(const char*);

typedef struct {
  uint x;
  uint y;
  u16 color;
  char ch;
  uint w;
  uint h;
  u16 *buffer;     // user-space pointer (nullable)
  uint stride;     // buffer stride in pixels (0 defaults to w)
  uint sx;         // sub-rect X in buffer
  uint sy;         // sub-rect Y in buffer
} fb_pixel_t;

typedef struct {
  int dx;
  int dy;
  int buttons;
} mouse_event_t;