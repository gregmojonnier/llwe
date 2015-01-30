/* A unique cursorless text editor. (c) 2015 Tom Wright */
#include <curses.h>
#include <ctype.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#define C_D 4
#define C_U 21

static char *filename, *buffer, *start, *end;
char errbuf[256];
static int bufsize, gap, gapsize, lwe_scroll;
typedef struct {
	char *start, *end;
} target;

static void
err(const char *str)
{
	snprintf(errbuf, sizeof(errbuf), "%s", str);
}

static int
bext(void)
{
	gapsize = bufsize;
	bufsize *= 2;
	buffer = realloc(buffer, bufsize);
	if (buffer == NULL) {
		err("memory");
		return 0;
	}
	return 1;
}

static int
bput(char c)
{
	buffer[gap++] = c;
	gapsize--;
	if (gapsize == 0)
		return bext();
	return 1;
}

static int
bins(char c, char *t)
{
	int sz;
	sz = gap - (t - buffer);
	memmove(t + 1, t, sz);
	*t = c;
	gap++;
	gapsize--;
	if (gapsize == 0)
		return bext();
	else
		return 1;
}

static int
bread(void)
{
	char c;
	FILE *f;
	bufsize = 4096;
	gap = 0;
	gapsize = bufsize;
	buffer = malloc(bufsize);
	if (buffer == NULL) {
		err("memory");
		return 0;
	}
	f = fopen(filename, "r");
	if (f == NULL) {
		return 1;
	}
	for (c = fgetc(f); c != EOF; c = fgetc(f))
		if (!bput(c))
			goto fail;
	fclose(f);
	return 1;
fail:	fclose(f);
	return 0;
}

int
breload()
{
	free(buffer);
	return bread();
}

static int
bsave(void)
{
	FILE *f = fopen(filename, "w");
	if (f == NULL) {
		err("write");
		return 0;
	}
	fwrite(buffer, 1, bufsize - gapsize, f);
	fclose(f);
	return 1;
}

static int
isend(const char *s)
{
	return s >= (buffer + bufsize - gapsize);
}

static void
winbounds(void)
{
	int r, c, i;
	r = c = 0;
	for (i = lwe_scroll, start = buffer; i > 0 && !isend(start);
	     (start)++)
		if (*start == '\n')
			i--;
	end = start;
loop:	if (isend(end))
		return;
	c++;
	if (*end == '\n') {
		c = 0;
	}
	c %= COLS;
	if (c == 0)
		r++;
	end++;
	if (r < LINES)
		goto loop;
	else
		end--;
}

static void
pc(char c)
{
	if (!isgraph(c) && !isspace(c))
		c = '?';
	addch(c);
}

static void
draw(void)
{
	char *i;
	erase();
	move(0, 0);
	winbounds();
	for (i = start; i != end; i++)
		pc(*i);
	refresh();
}

static void
doscrl(int d)
{
	lwe_scroll += d;
	if (lwe_scroll < 0)
		lwe_scroll = 0;
}

static void
jumptoline(void)
{
	char buf[32], c;
	int i;
	memset(buf, '\0', 32);
	for (i = 0; i < 32; i++) {
		c = getch();
		if (isdigit(c))
			buf[i] = c;
		else
			break;
	}
	i = atoi(buf);
	if (i == 0)
		return;
	lwe_scroll = i;
	doscrl(-LINES / 2);
}

static char *
find(char c, int n)
{
	char *i;
	for (i = start; i < end; i++) {
		if (*i == c) {
			if (n <= 0)
				return i;
			else
				n--;
		}
	}
	return 0;
}

static int
count(char c)
{
	int ct;
	char *i;
	ct = 0;
	for (i = start; i != end; i++)
		if (*i == c)
			ct++;
	return ct;
}

static void
ptarg(int count)
{
	char a;
	a = 'a' + (count % 26);
	attron(A_STANDOUT);
	pc(a);
	attroff(A_STANDOUT);
}

static int
skips(int lvl)
{
	int i;
	for (i = 1; lvl > 0; lvl--)
		i *= 26;
	return i;
}

static void
drawdisamb(char c, int lvl, int off)
{
	char *i;
	int ct;
	erase();
	move(0, 0);
	ct = 0;
	for (i = start; i < end; i++) {
		if (*i == c && off > 0) {
			pc(*i);
			off--;
		} else if (*i == c && off <= 0) {
			ptarg(ct++);
			off = skips(lvl) - 1;
		} else {
			pc(*i);
		}
	}
	refresh();
}

static char *
disamb(char c, int lvl, int off)
{
	char inp;
	int i;
	if (count(c) - off <= skips(lvl))
		return find(c, off);
	drawdisamb(c, lvl, off);
	inp = getch();
	i = inp - 'a';
	if (i < 0 || i > 26)
		return 0;
	return disamb(c, lvl + 1, off + i * skips(lvl));
}

static char *
hunt(void)
{
	char c;
	if (gapsize == bufsize)
		return buffer;
	draw();
	c = getch();
	return disamb(c, 0, 0);
}

static void
rubout(char *t)
{
	int sz;
	sz = gap - (t + 1 - buffer);
	memmove(t, t + 1, sz);
	gapsize++;
	gap--;
}

static int
insertmode(char *t)
{
	int c;
	for (;;) {
		draw();
		if (t > end)
			doscrl(LINES / 2);
		c = getch();
		if (c == '\r')
			c = '\n';
		if (c == C_D)
			return 1;
		if (c == KEY_BACKSPACE) {
			if (t <= buffer)
				continue;
			t--;
			rubout(t);
			continue;
		}
		if (!isgraph(c) && !isspace(c)) {
			continue;
		}
		if (!bins(c, t))
			return 0;
		else
			t++;
	}
	return 1;
}

static void
delete(target t)
{
	int n, tn;
	if (t.end != buffer + bufsize)
		t.end++;
	n = buffer + bufsize - t.end;
	tn = t.end - t.start;
	memmove(t.start, t.end, n);
	gap -= tn;
	gapsize += tn;
}

static int
cmdloop(void)
{
	int q, c;
	target t;
	for (q = 0; q == 0;) {
		draw();
		c = getch();
		switch (c) {
		case C_D:
		case KEY_DOWN:
		case KEY_NPAGE:
		case 'j':
			doscrl(LINES / 2);
			break;
		case C_U:
		case KEY_UP:
		case KEY_PPAGE:
		case 'k':
			doscrl(-LINES / 2);
			break;
		case 'q':
		case EOF:
			q = 1;
			break;
		case 'i':
			t.start = hunt();
			if (t.start == 0)
				break;
			if (!insertmode(t.start))
				return 0;
			break;
		case 'a':
			t.start = hunt();
			if (t.start == 0)
				break;
			if (t.start != buffer + bufsize)
				t.start++;
			if (!insertmode(t.start))
				return 0;
			break;
		case 'w':
			if (!bsave())
				return 0;
			break;
		case 'd':
			t.start = hunt();
			t.end = hunt();
			if (t.start == 0 || t.end == 0)
				break;
			delete(t);
			break;
		case 'c':
			t.start = hunt();
			t.end = hunt();
			if (t.start == 0 || t.end == 0)
				break;
			delete(t);
			insertmode(t.start);
			break;
		case 'r':
			breload();
			break;
		case 'g':
			jumptoline();
			break;
		}
	}
	return 1;
}

static void
ed(void)
{
	if (!bread())
		return;
	lwe_scroll = 0;
	cmdloop();
}

int
main(int argc, char **argv)
{
	if (argc != 2) {
		err("missing file arg");
		return 2;
	} else {
		initscr();
		cbreak();
		noecho();
		nonl();
		intrflush(stdscr, FALSE);
		keypad(stdscr, TRUE);
		filename = argv[1];
		ed();
		endwin();
		if (strcmp(errbuf, ""))
			fprintf(stderr, "error: %s\n", errbuf);
		return 0;
	}
}
