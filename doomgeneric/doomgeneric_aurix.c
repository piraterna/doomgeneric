//doomgeneric for AurixOS

#include "doomkeys.h"
#include "m_argv.h"
#include "doomgeneric.h"

#include <fcntl.h>
#include <stdio.h>
#include <unistd.h>
#include <termios.h>
#include <time.h>

#include <stdbool.h>

#define KEYQUEUE_SIZE 16

static unsigned short s_KeyQueue[KEYQUEUE_SIZE];
static unsigned int s_KeyQueueWriteIndex = 0;
static unsigned int s_KeyQueueReadIndex = 0;

static struct timespec start_time = {0};

int framebuffer;
int input;
int input;

static unsigned char convertToDoomKey(unsigned int key)
{
  switch (key) {
    case '\n':
      key = KEY_ENTER;
      break;
    case 'w':
    case 'W':
      key = KEY_UPARROW;
      break;
    case 's':
    case 'S':
      key = KEY_DOWNARROW;
      break;
    case 'a':
    case 'A':
      key = KEY_LEFTARROW;
      break;
    case 'd':
    case 'D':
      key = KEY_RIGHTARROW;
    default:
      key = KEY_FIRE;
      break;
  }
  return key;
}

static void addKeyToQueue(int pressed, unsigned int keyCode)
{
  unsigned char key = convertToDoomKey(keyCode);

  unsigned short keyData = (pressed << 8) | key;

  s_KeyQueue[s_KeyQueueWriteIndex] = keyData;
  s_KeyQueueWriteIndex++;
  s_KeyQueueWriteIndex %= KEYQUEUE_SIZE;
}

static void handleKeyInput()
{
  char keybuf[4];
  int keys = read(fileno(stdin), keybuf, sizeof(keybuf));
  if (keys) {
    addKeyToQueue(2, keybuf[keys - 1]);
    addKeyToQueue(1, keybuf[keys - 1]);
    addKeyToQueue(0, keybuf[keys - 1]);
  }
}

void DG_Init()
{
  framebuffer = open("/dev/fb", O_WRONLY);
  if (framebuffer < 0) {
    printf("failed to open framebuffer\n");
    exit(1);
  }

  // NOTE: O_NONBLOCK is not supported atm, so DOOM is not really playable
  input = open("/dev/stdin", O_RDONLY | O_NONBLOCK);
  if (input < 0) {
    printf("failed to open input device\n");
    exit(1);
  }

  clock_gettime(CLOCK_REALTIME, &start_time);

  struct termios term;
  tcgetattr(input, &term);

  term.c_lflag &= ~(ICANON | ECHO);
  tcsetattr(input, 0, &term);
}

void DG_DrawFrame()
{
  write(framebuffer, DG_ScreenBuffer, DOOMGENERIC_RESX * DOOMGENERIC_RESY);

  handleKeyInput();
}

void DG_SleepMs(uint32_t ms)
{
  usleep(ms * 1000);
}

uint32_t DG_GetTicksMs()
{
  struct timespec cur_time;
  clock_gettime(CLOCK_REALTIME, &cur_time);
  
  long s, us;

  s = cur_time.tv_sec - start_time.tv_sec;
  us = (cur_time.tv_nsec - start_time.tv_nsec) / 1000;

	return (s * 1000) + (us / 1000);
}

int DG_GetKey(int* pressed, unsigned char* doomKey)
{
  if (s_KeyQueueReadIndex == s_KeyQueueWriteIndex){
    //key queue is empty
    return 0;
  }else{
    unsigned short keyData = s_KeyQueue[s_KeyQueueReadIndex];
    s_KeyQueueReadIndex++;
    s_KeyQueueReadIndex %= KEYQUEUE_SIZE;

    *pressed = keyData >> 8;
    *doomKey = keyData & 0xFF;

    return 1;
  }

  return 0;
}

void DG_SetWindowTitle(const char *title)
{
  (void)title;
}

int main(int argc, char **argv)
{
    doomgeneric_Create(argc, argv);

    for (int i = 0;; i++) {
        doomgeneric_Tick();
    }
    

    return 0;
}