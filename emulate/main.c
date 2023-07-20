#include <sys/types.h>

#include <sys/socket.h>
#include <arpa/inet.h>
#include <netinet/in.h>

#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <limits.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <SDL.h>

static uint64_t		 framecnt = 0;

/* BEGIN LED THINGS */

#define RMT_NUM_LEDS		16
#define ACTIVE_SIZE		(RMT_NUM_LEDS * sizeof(struct pixel))

#define LED_COLOR_RED		1
#define LED_COLOR_GREEN		2
#define LED_COLOR_BLUE		4

#define LED_COLOR_YELLOW	(LED_COLOR_RED | LED_COLOR_GREEN)
#define LED_COLOR_TURQUOISE	(LED_COLOR_GREEN | LED_COLOR_BLUE)
#define LED_COLOR_PURPLE	(LED_COLOR_BLUE | LED_COLOR_RED)

#define STATE_SOLID		0
#define STATE_BLINK_UP		1
#define STATE_BLINK_DOWN	2
#define STATE_SPIN_UP		3
#define STATE_SPIN		4

/* NOTE: must cleanly divide 255 for maximum effect */
#define ANIMATION_FPS		85

#define SPIN_DELTA		(255 / (RMT_NUM_LEDS / 2) + 1)
#define SPIN_HALFWAY		(255 - (SPIN_DELTA * RMT_NUM_LEDS / 2))
#define BLINK_INCREMENT		(255 / ANIMATION_FPS)

struct __attribute__((packed)) pixel {
	uint8_t	g;
	uint8_t r;
	uint8_t b;
};

static int		 transitionpt = 0;
static int		 spinready = 0;
static int		 primaryidx, secondaryidx;

static int		 state = STATE_SOLID;
static struct pixel	 actives[RMT_NUM_LEDS];
static struct pixel	 reference = { 0 };

static struct pixel	 color_to_pixel(uint8_t);
static void		 update_indices(struct pixel);
static void		 update_spin(void);
static void		 prep_spin(void);
static int		 led_spin(uint8_t);

static struct pixel
color_to_pixel(uint8_t color)
{
	struct pixel	ret = { 0 };

	if (color & LED_COLOR_RED) ret.r = 255;
	if (color & LED_COLOR_GREEN) ret.g = 255;
	if (color & LED_COLOR_BLUE) ret.b = 255;

	return ret;
}

static void
update_indices(struct pixel p)
{
	int	 i;
	uint8_t	*packed = (uint8_t *)&p;

	/* composite colors */
	for (i = 0; i < 3; i++)
		if (packed[i] && packed[(i + 1) % 3]) {
			primaryidx = (i + 1) % 3;
			secondaryidx = i;
			return;
		}

	/* primary colors */
	for (i = 0; i < 3; i++) if (packed[i]) break;
	secondaryidx = (i + 1) % 3;
	primaryidx = i;
}

static void
prep_spin(void)
{
	struct pixel	 refcopy;
	uint8_t		*packed, *rpacked;
	int	 	 i;

	for (i = 0; i < RMT_NUM_LEDS; i++) {
		refcopy = reference;

		rpacked = (uint8_t *)(&refcopy);
		packed = (uint8_t *)(&actives[i]);

		if (i < RMT_NUM_LEDS / 2) rpacked[secondaryidx] = i * SPIN_DELTA;
		else rpacked[secondaryidx] = 255 - ((i - (RMT_NUM_LEDS / 2)) * SPIN_DELTA);

		packed[primaryidx] += BLINK_INCREMENT;
		if (packed[primaryidx] > 255 - rpacked[secondaryidx])
			packed[secondaryidx] = packed[primaryidx] - (255 - rpacked[secondaryidx]);
		if (packed[primaryidx] == 255) {
			printf("spin ready!\n");
			spinready = 1;
		}
	}

	printf("spin up at primary idx = %d (pi = %d, si = %d)\n", packed[primaryidx], primaryidx, secondaryidx);
}

static void
update_spin(void)
{
	int		 i, cnt = 0;
	uint8_t		*packed;

	for (i = transitionpt; cnt < RMT_NUM_LEDS - 1; cnt++) {
		packed = (uint8_t *)(&actives[i]);
		packed[secondaryidx] += (cnt < RMT_NUM_LEDS / 2) ? 1 : -1;
		i = (i + 1) % RMT_NUM_LEDS;
	}

	packed = (uint8_t *)(&actives[i]);
	packed[secondaryidx] -= 1;
	if (packed[secondaryidx] == 0) {
		printf("[fc = %llu] transition point %d -> %d\n", framecnt, transitionpt, i);
		transitionpt = i;
	}
}

static int
led_spin(uint8_t color)
{
	reference = color_to_pixel(color);
	update_indices(reference);

	memset(actives, 0, ACTIVE_SIZE);
	state = STATE_SPIN_UP;
	transitionpt = 0;
	spinready = 0;

	return 0;
}

/* END LED THINGS */

#define APP_NAME	"juliana.jtlang.dev"

#define SDL_ERROR(S)	printf("%s: %s", S, SDL_GetError())

#define SCREEN_WIDTH	640
#define SCREEN_HEIGHT	480

#define OUTER_PADDING	50
#define INNER_RADIUS	150
#define THICKNESS	(SCREEN_HEIGHT / 2 - OUTER_PADDING - INNER_RADIUS)
#define OUTER_RADIUS	(INNER_RADIUS + THICKNESS)

#define INNER_PADDING	5
#define LED_RADIUS	(THICKNESS / 2 - 5)
#define LED_MIDPOINT	(INNER_RADIUS + THICKNESS / 2)

#define PI_INCREMENT	(M_PI / (RMT_NUM_LEDS / 2))

#define FRAME_DELAY	(1000 / ANIMATION_FPS)

struct sdlcb {
	int	(*ucb)(void *);
	void	 *arg;
};

static SDL_Window	*window = NULL;
static SDL_Renderer	*renderer = NULL;
static SDL_Texture	*texture = NULL;

/* what color the LEDs are spinning right now */
static uint8_t		 currentcolor = LED_COLOR_GREEN;

static int		 sockfd;

static void		 index_to_position(int, uint32_t, uint32_t *, uint32_t *);
static void		 draw_circle(SDL_Renderer *, uint32_t, uint32_t,
		    	     uint32_t, struct pixel);

static uint8_t		 poll_color(void);
static int		 try_increment_color(uint8_t);

static void
index_to_position(int index, uint32_t radius, uint32_t *x, uint32_t *y)
{
	double	rawx, rawy, angle;

	angle = (double)index * PI_INCREMENT;
	rawx = (double)radius * cos(angle);
	rawy = (double)radius * sin(angle);

	*x = (uint32_t)(rawx + SCREEN_WIDTH / 2);
	*y = (uint32_t)(rawy + SCREEN_HEIGHT / 2);
}

static void
draw_circle(SDL_Renderer *r, uint32_t x, uint32_t y,
    uint32_t radius, struct pixel p)
{
	int	ox, oy, d;

	ox = 0;
	oy = (int)radius;
	d = (int)radius - 1;

	SDL_SetRenderDrawColor(r, p.r, p.g, p.b, 255);

	while (oy >= ox) {
		SDL_RenderDrawLine(r, x - oy, y + ox, x + oy, y + ox);
		SDL_RenderDrawLine(r, x - ox, y + oy, x + ox, y + oy);
		SDL_RenderDrawLine(r, x - ox, y - oy, x + ox, y - oy);
		SDL_RenderDrawLine(r, x - oy, y - ox, x + oy, y - ox);

		if (d >= 2 * ox) {
			d -= 2 * ox + 1;
			ox += 1;
		} else if (d < 2 * ((int)radius - oy)) {
			d += 2 * oy - 1;
			oy -= 1;
		} else {
			d += 2 * (oy - ox - 1);
			oy -= 1;
			ox += 1;
		}
	}
}

static uint8_t
poll_color(void)
{
	uint8_t	new;

	if (read(sockfd, &new, sizeof(uint8_t)) < 0) {
		if (errno == EAGAIN) return currentcolor;
		err(1, "read");
	}

	return new;
}

static int
try_increment_color(uint8_t nextcolor)
{
	if (write(sockfd, &nextcolor, sizeof(uint8_t)) > 0) return 1;
	if (errno != EAGAIN) err(1, "write");
	else return 0;
}

int
main(int argc, char *argv[])
{
	struct hostent		*host;
	struct sockaddr_in	 sa;
	int			 flags;

	struct pixel		 black = { 0 };
	struct pixel		 ring = { .r = 70, .g = 70, .b = 70 };

	sockfd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
	if (sockfd < 0) err(1, "socket");

	if ((host = gethostbyname(APP_NAME)) == NULL) {
		close(sockfd);
		errx(1, "gethostbyname (h_errno = %d)", h_errno);
	}

	sa.sin_family = AF_INET;
	sa.sin_port = htons(6969);
	sa.sin_addr.s_addr = *(in_addr_t *)host->h_addr;

	if (connect(sockfd, (struct sockaddr *)&sa,
	    sizeof(struct sockaddr_in)) < 0) {
	    	close(sockfd);
		err(1, "connect");
	}

	if ((flags = fcntl(sockfd, F_GETFL)) < 0) err(1, "fcntl F_GETFL");
	flags |= O_NONBLOCK;
	if (fcntl(sockfd, F_SETFL, flags) < 0) err(1, "fcntl F_SETFL");

	if ((flags = fcntl(sockfd, F_GETFD)) < 0) err(1, "fcntl F_GETFD");
	flags |= O_CLOEXEC;
	if (fcntl(sockfd, F_SETFD, flags) < 0) err(1, "fcntl F_SETFD");

	if (SDL_InitSubSystem(SDL_INIT_VIDEO) < 0) {
		SDL_ERROR("SDL_InitSubsystem");
		goto end;
	}

	window = SDL_CreateWindow("LED Emulator",
	    SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
	    SCREEN_WIDTH, SCREEN_HEIGHT,
	    SDL_WINDOW_ALLOW_HIGHDPI | SDL_WINDOW_SHOWN);

	if (window == NULL) {
		SDL_ERROR("SDL_CreateWindow");
		goto end;
	}

	renderer = SDL_CreateRenderer(window, -1,
	    SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
	if (renderer == NULL) {
		SDL_ERROR("SDL_CreateRenderer");
		goto end;
	}

	texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGB888,
	    SDL_TEXTUREACCESS_TARGET,
	    SCREEN_WIDTH, SCREEN_HEIGHT);

	if (texture == NULL) {
		SDL_ERROR("SDL_CreateTexture");
		goto end;
	}

	SDL_memset(actives, 0, ACTIVE_SIZE);
	currentcolor = poll_color();
	led_spin(currentcolor);

	for (;;) {
		SDL_Event	event;
		int		i, pending = 0;
		uint8_t		nextcolor;

		/* check if we got interrupted */
		while (SDL_PollEvent(&event)) {
			if (event.type == SDL_QUIT) goto end;
			else if (event.type == SDL_KEYDOWN) {
				if (event.key.keysym.sym == SDLK_SPACE)
					pending = 1;
			}
		}

		if (pending) {
			if (currentcolor == 6) nextcolor = 1;
			else nextcolor = currentcolor + 1;

			pending = !try_increment_color(nextcolor);
		}

		/* do we need to update the LED color? */
		nextcolor = poll_color();
		if (nextcolor != currentcolor) {
			currentcolor = nextcolor;
			led_spin(currentcolor);
		}

		/* make per-frame updates to the LEDs */
		if (spinready) update_spin();
		else prep_spin();

		/* render the LEDs */
		SDL_SetRenderTarget(renderer, texture);

		draw_circle(renderer, SCREEN_WIDTH / 2, SCREEN_HEIGHT / 2,
		    OUTER_RADIUS, ring);
		draw_circle(renderer, SCREEN_WIDTH / 2, SCREEN_HEIGHT / 2,
		    INNER_RADIUS, black);

		for (i = 0; i < RMT_NUM_LEDS; i++) {
			uint32_t cx, cy;
			index_to_position(i, LED_MIDPOINT, &cx, &cy);
			draw_circle(renderer, cx, cy, LED_RADIUS, actives[i]);
		}

		SDL_SetRenderTarget(renderer, NULL);
		SDL_RenderCopy(renderer, texture, NULL, NULL);
		SDL_RenderPresent(renderer);

		SDL_Delay(FRAME_DELAY);
	}

end:
	return 0;

	(void)argc;
	(void)argv;
}
