/* led.c
 * high-level interface to the LEDs
 *
 * (c) jay lang, 2023
 * redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 * 
 * 1. redistributions of source code must retain the above copyright notice,
 * this list of conditions and the following disclaimer.
 * 
 * 2. redistributions in binary form must reproduce the above copyright
 * notice, this list of conditions and the following disclaimer in the
 * documentation and/or other materials provided with the distribution.
 * 
 * 3. neither the name of the copyright holder nor the names of its
 * contributors may be used to endorse or promote products derived from this
 * software without specific prior written permission.
 * 
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS “AS IS”
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#include <stdint.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>

#include "him.h"

#define ACTIVE_SIZE		(RMT_NUM_LEDS * sizeof(struct pixel))

#define STATE_SOLID		0
#define STATE_BLINK		1
#define STATE_SPINNY		2

#define BLINK_STATE_UP		0
#define BLINK_STATE_DOWN	1

/* NOTE: must cleanly divide 255 for maximum effect */
#define ANIMATION_FPS		255

LOG_SET_TAG("led");

struct __attribute__((packed)) pixel {
	uint8_t	g;
	uint8_t r;
	uint8_t b;
};

static int		 state = STATE_SOLID;
static struct pixel	*actives = NULL;
static struct pixel	 reference = { 0 };

static int		 blink_state;

static struct pixel	 color_to_pixel(uint8_t);
static int		 blink_cb(void);
static struct pixel	 pixel_to_secondary(struct pixel p);

esp_err_t
led_init(void)
{
	actives = calloc(RMT_NUM_LEDS, sizeof(struct pixel));
	if (actives == NULL) CATCH_RETURN(errno);

	if (led_solid(0) != 0) {
		free(actives);
		CATCH_RETURN(errno);
	}

	return 0;
}

void
led_teardown(void)
{
	free(actives);
	actives = NULL;
}

static struct pixel
color_to_pixel(uint8_t color)
{
	struct pixel	ret = { 0 };

	if (color & LED_COLOR_RED) ret.r = 255;
	if (color & LED_COLOR_GREEN) ret.g = 255;
	if (color & LED_COLOR_BLUE) ret.b = 255;

	return ret;
}

esp_err_t
led_solid(uint8_t color)
{
	int		i;
	struct pixel	p;

	if (color >= LED_COLOR_MAX) CATCH_RETURN(EINVAL);
	else p = color_to_pixel(color);

	for (i = 0; i < RMT_NUM_LEDS; i++) actives[i] = p;
	CATCH_RETURN(rmt_enqueue(actives, ACTIVE_SIZE));

	state = STATE_SOLID;
	reference = p;

	return 0;
}

static int
blink_cb(void)
{
	/* we want to go all the way down in one second,
	 * and vice versa. this should get us there
	 */
	uint8_t		inc = 255 / ANIMATION_FPS;
	int		i;

	if (state != STATE_BLINK) return SCHED_STOP;

	if (reference.r > 0) {
		for (i = 0; i < RMT_NUM_LEDS; i++) {
			if (blink_state == BLINK_STATE_UP) actives[i].r += inc;
			else actives[i].r -= inc;
		}
	}

	if (reference.g > 0) {
		for (i = 0; i < RMT_NUM_LEDS; i++) {
			if (blink_state == BLINK_STATE_UP) actives[i].g += inc;
			else actives[i].g -= inc;
		}
	}

	if (reference.b > 0) {
		for (i = 0; i < RMT_NUM_LEDS; i++) {
			if (blink_state == BLINK_STATE_UP) actives[i].b += inc;
			else actives[i].b -= inc;
		}
	}

	if (blink_state == BLINK_STATE_UP) {
		if (actives->r == 255) blink_state = BLINK_STATE_DOWN;
		else if (actives->g == 255) blink_state = BLINK_STATE_DOWN;
		else if (actives->b == 255) blink_state = BLINK_STATE_DOWN;
	} else {
		if (actives->r == 0 && reference.r != 0)
			blink_state = BLINK_STATE_UP;
		else if (actives->g == 0 && reference.g != 0)
			blink_state = BLINK_STATE_UP;
		else if (actives->b == 0 && reference.b != 0)
			blink_state = BLINK_STATE_UP;
	}

	rmt_enqueue(actives, ACTIVE_SIZE);

	return SCHED_CONTINUE;
}

esp_err_t
led_blink(uint8_t color)
{
	struct pixel	p;

	if (color >= LED_COLOR_MAX) CATCH_RETURN(EINVAL);
	else p = color_to_pixel(color);

	memset(actives, 0, ACTIVE_SIZE);
	CATCH_RETURN(rmt_enqueue(actives, ACTIVE_SIZE));
	CATCH_RETURN(sched_schedule(SCHED_US_PER_S / ANIMATION_FPS, blink_cb));

	state = STATE_BLINK;
	reference = p;
	blink_state = BLINK_STATE_UP;
	return 0;
}

/* spinny effect
 * i like the idea of having two colors
 * red-purple
 * yellow-orange
 * green-yellowish
 * turquoise-green
 * blue-turquoise / sea blue
 * purple-blue
 */
static struct pixel
pixel_to_secondary(struct pixel p)
{
	struct pixel	s;

	
}


esp_err_t
led_spinny(uint8_t color)
{
	struct pixel	p;


}