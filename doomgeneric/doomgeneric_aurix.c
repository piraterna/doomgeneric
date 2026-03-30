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

#define KEYQUEUE_SIZE 128

struct keymap_entry {
	unsigned char scancode;
	bool extended;
	bool set2;
	unsigned char doom_key;
};

static unsigned short s_KeyQueue[KEYQUEUE_SIZE];
static unsigned int s_KeyQueueWriteIndex = 0;
static unsigned int s_KeyQueueReadIndex = 0;
static unsigned int s_KeyQueueCount = 0;
static int s_PendingKeyRelease = -1;
static bool s_ScancodeExtended = false;
static bool s_ScancodeReleasePrefix = false;
static bool s_UsingSet2Scancodes = false;

static const struct keymap_entry s_ScancodeKeyMap[] = {
	{ 0x01, false, false, KEY_ESCAPE },
	{ 0x1C, false, false, KEY_ENTER },
	{ 0x0F, false, false, KEY_TAB },
	{ 0x0E, false, false, KEY_BACKSPACE },
	{ 0x1D, false, false, KEY_FIRE },
	{ 0x2A, false, false, KEY_RSHIFT },
	{ 0x36, false, false, KEY_RSHIFT },
	{ 0x38, false, false, KEY_RALT },
	{ 0x39, false, false, KEY_USE },
	{ 0x11, false, false, KEY_UPARROW },
	{ 0x1E, false, false, KEY_LEFTARROW },
	{ 0x1F, false, false, KEY_DOWNARROW },
	{ 0x20, false, false, KEY_RIGHTARROW },
	{ 0x48, false, false, KEY_UPARROW },
	{ 0x4B, false, false, KEY_LEFTARROW },
	{ 0x50, false, false, KEY_DOWNARROW },
	{ 0x4D, false, false, KEY_RIGHTARROW },
	{ 0x48, true, false, KEY_UPARROW },
	{ 0x4B, true, false, KEY_LEFTARROW },
	{ 0x50, true, false, KEY_DOWNARROW },
	{ 0x4D, true, false, KEY_RIGHTARROW },

	{ 0x76, false, true, KEY_ESCAPE },
	{ 0x5A, false, true, KEY_ENTER },
	{ 0x0D, false, true, KEY_TAB },
	{ 0x66, false, true, KEY_BACKSPACE },
	{ 0x14, false, true, KEY_FIRE },
	{ 0x12, false, true, KEY_RSHIFT },
	{ 0x59, false, true, KEY_RSHIFT },
	{ 0x11, false, true, KEY_RALT },
	{ 0x29, false, true, KEY_USE },
	{ 0x1D, false, true, KEY_UPARROW },
	{ 0x1C, false, true, KEY_LEFTARROW },
	{ 0x1B, false, true, KEY_DOWNARROW },
	{ 0x23, false, true, KEY_RIGHTARROW },
	{ 0x75, true, true, KEY_UPARROW },
	{ 0x6B, true, true, KEY_LEFTARROW },
	{ 0x72, true, true, KEY_DOWNARROW },
	{ 0x74, true, true, KEY_RIGHTARROW },
};

static struct timespec start_time = { 0 };

int framebuffer;
int input;
int raw_keyboard = -1;

static unsigned char convertToDoomKey(unsigned int key)
{
	switch (key) {
	case '\n':
		key = KEY_ENTER;
		break;
	case ' ':
		key = KEY_USE;
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
		break;
	default:
		key = 0;
		break;
	}
	return key;
}

static unsigned char convertScancodeToDoomKey(unsigned int key, bool extended,
											  bool set2)
{
	for (unsigned int i = 0;
		 i < (sizeof(s_ScancodeKeyMap) / sizeof(s_ScancodeKeyMap[0])); i++) {
		if (s_ScancodeKeyMap[i].scancode == (unsigned char)key &&
			s_ScancodeKeyMap[i].extended == extended &&
			s_ScancodeKeyMap[i].set2 == set2) {
			return s_ScancodeKeyMap[i].doom_key;
		}
	}

	return 0;
}

static void addDoomKeyToQueue(int pressed, unsigned char key)
{
	if (!key)
		return;

	unsigned short keyData = (pressed << 8) | key;

	if (s_KeyQueueCount >= KEYQUEUE_SIZE) {
		s_KeyQueueReadIndex++;
		s_KeyQueueReadIndex %= KEYQUEUE_SIZE;
		s_KeyQueueCount--;
	}

	s_KeyQueue[s_KeyQueueWriteIndex] = keyData;
	s_KeyQueueWriteIndex++;
	s_KeyQueueWriteIndex %= KEYQUEUE_SIZE;
	s_KeyQueueCount++;
}

static void addKeyToQueue(int pressed, unsigned int keyCode)
{
	unsigned char key = convertToDoomKey(keyCode);
	addDoomKeyToQueue(pressed, key);
}

static void handleKeyInput()
{
	if (raw_keyboard >= 0) {
		char scbuf[32];

		while (1) {
			int keys = read(raw_keyboard, scbuf, sizeof(scbuf));
			if (keys <= 0)
				break;

			for (int i = 0; i < keys; i++) {
				unsigned char sc = (unsigned char)scbuf[i];

				if (sc == 0xE0) {
					s_ScancodeExtended = true;
					continue;
				}

				if (sc == 0xF0) {
					s_UsingSet2Scancodes = true;
					s_ScancodeReleasePrefix = true;
					continue;
				}

				bool released = s_ScancodeReleasePrefix;
				s_ScancodeReleasePrefix = false;

				if (sc & 0x80) {
					released = true;
					sc &= 0x7F;
				}

				unsigned char key = convertScancodeToDoomKey(
					sc, s_ScancodeExtended, s_UsingSet2Scancodes);
				s_ScancodeExtended = false;

				addDoomKeyToQueue(released ? 0 : 1, key);
			}
		}

		return;
	}

	if (s_PendingKeyRelease >= 0) {
		addKeyToQueue(0, (unsigned int)s_PendingKeyRelease);
		s_PendingKeyRelease = -1;
	}

	char keybuf[64];
	unsigned char latest = 0;
	bool have_key = false;

	while (1) {
		int keys = read(input, keybuf, sizeof(keybuf));
		if (keys <= 0)
			break;

		latest = (unsigned char)keybuf[keys - 1];
		have_key = true;
	}

	if (have_key) {
		addKeyToQueue(1, latest);
		s_PendingKeyRelease = latest;
	}
}

void DG_Init()
{
	framebuffer = open("/dev/fb", O_WRONLY);
	if (framebuffer < 0) {
		printf("failed to open framebuffer\n");
		exit(1);
	}

	input = fileno(stdin);
	if (input < 0) {
		printf("failed to get stdin fd\n");
		exit(1);
	}

	int flags = fcntl(input, F_GETFL);
	if (flags < 0 || fcntl(input, F_SETFL, flags | O_NONBLOCK) < 0) {
		printf("failed to set stdin nonblocking\n");
		exit(1);
	}

	raw_keyboard = open("/dev/raw/ps2/kbd0", O_RDONLY);
	if (raw_keyboard >= 0) {
		int raw_flags = fcntl(raw_keyboard, F_GETFL);
		if (raw_flags >= 0)
			(void)fcntl(raw_keyboard, F_SETFL, raw_flags | O_NONBLOCK);
	}

	clock_gettime(CLOCK_REALTIME, &start_time);

	struct termios term;
	tcgetattr(input, &term);

	term.c_lflag &= ~(ICANON | ECHO);
	tcsetattr(input, 0, &term);
}

void DG_DrawFrame()
{
	lseek(framebuffer, 0, SEEK_SET);
	write(framebuffer, DG_ScreenBuffer,
		  DOOMGENERIC_RESX * DOOMGENERIC_RESY * sizeof(*DG_ScreenBuffer));

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

int DG_GetKey(int *pressed, unsigned char *doomKey)
{
	if (s_KeyQueueCount == 0) {
		//key queue is empty
		return 0;
	} else {
		unsigned short keyData = s_KeyQueue[s_KeyQueueReadIndex];
		s_KeyQueueReadIndex++;
		s_KeyQueueReadIndex %= KEYQUEUE_SIZE;
		s_KeyQueueCount--;

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
