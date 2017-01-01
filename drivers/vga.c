#include <stdint.h>
#include <string.h>

unsigned char inportb (unsigned short _port)
{
    unsigned char rv;
    __asm__ __volatile__ ("inb %1, %0" : "=a" (rv) : "dN" (_port));
    return rv;
}

void outportb (unsigned short _port, unsigned char _data)
{
    __asm__ __volatile__ ("outb %1, %0" : : "dN" (_port), "a" (_data));
}

#define FB_CMD_PORT 0x3d4
#define FB_DATA_PORT 0x3d5
#define FB_CMD_HIGH 14
#define FB_CMD_LOW 15
#define WHITE_COLOR 0xf
#define BLACK_COLOR 0x0
#define COLOR_ATTR (BLACK_COLOR << 4 | WHITE_COLOR)

static uint16_t *fb;
static int _x, _y;

static void move_cursor(int x, int y)
{
	int pos = y * 80 + x;

	outportb(FB_CMD_PORT, FB_CMD_HIGH);
	outportb(FB_DATA_PORT, (pos >> 8 & 0xff));
	outportb(FB_CMD_PORT, FB_CMD_LOW);
	outportb(FB_DATA_PORT, (pos & 0xff));
}

/* Clears the screen */
static void cls()
{
	uint16_t blank;
	int i;

	/* Again, we need the 'short' that will be used to
	 * represent a space with color */
	blank = 0x20 | (0xf << 8);

	/* Sets the entire screen to spaces in our current
	 * color */
	for (i = 0; i < 25; i++)
		memsetw (fb + i * 80, blank, 80);

	/* Update out virtual cursor, and then move the
	 * hardware cursor */
	_x = 0;
	_y = 0;
	move_cursor(_x, _y);
}

int k_write_char(char c)
{
	uint16_t *loc = fb + (_y * 80 + _x);
	uint8_t attr = COLOR_ATTR;

	*loc = c | (attr << 8);
	_x++;
}

void k_video_init()
{
	fb = (uint16_t *) 0xB8000;
	cls();
}
