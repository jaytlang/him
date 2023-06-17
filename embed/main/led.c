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
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "him.h"

#define ACTIVE_SIZE		(RMT_NUM_LEDS * sizeof(struct pixel))

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

LOG_SET_TAG("led");

struct __attribute__((packed)) pixel {
	uint8_t	g;
	uint8_t r;
	uint8_t b;
};

static uint64_t		 epoch = 0;
static int		 transitionpt = 0;
static int		 primaryidx, secondaryidx;

static int		 state = STATE_SOLID;
static struct pixel	*actives = NULL;
static struct pixel	 reference = { 0 };

static esp_err_t	 epoch_new(uint64_t **);

static struct pixel	 color_to_pixel(uint8_t);
static void		 update_indices(struct pixel);

static void		 prep_spin(void);
static void		 update_spin(void);

static int		 blink_cb(void *);
static int		 spin_cb(void *);

static esp_err_t
epoch_new(uint64_t **e)
{
	if ((*e = malloc(sizeof(uint64_t))) == NULL)
		CATCH_RETURN(errno);
	**e = ++epoch;
	return 0;
}

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

esp_err_t
led_solid(uint8_t color)
{
	int		 i;
	struct pixel	 p;

	/* knock out the previous effect without alloc'ing */
	epoch++;

	if (color >= LED_COLOR_MAX) CATCH_RETURN(EINVAL);
	else p = color_to_pixel(color);

	for (i = 0; i < RMT_NUM_LEDS; i++) actives[i] = p;
	CATCH_RETURN(rmt_enqueue(actives, ACTIVE_SIZE));

	state = STATE_SOLID;
	reference = p;

	return 0;
}

static int
blink_cb(void *arg)
{
	/* we want to go all the way down in one second,
	 * and vice versa. this should get us there
	 */
	int		 i;
	uint64_t	*myepoch = (uint64_t *)arg;

	if (*myepoch != epoch) {
		free(myepoch);
		return SCHED_STOP;
	}

	for (i = 0; i < RMT_NUM_LEDS; i++) {
		if (state == STATE_BLINK_UP) {
			actives[i].r += (reference.r > 0) * BLINK_INCREMENT;
			actives[i].g += (reference.g > 0) * BLINK_INCREMENT;
			actives[i].b += (reference.b > 0) * BLINK_INCREMENT;
		} else {
			actives[i].r -= (reference.r > 0) * BLINK_INCREMENT;
			actives[i].g -= (reference.g > 0) * BLINK_INCREMENT;
			actives[i].b -= (reference.b > 0) * BLINK_INCREMENT;
		}
	}

	if (state == STATE_BLINK_UP &&
	    (actives->r == 255 ||
	     actives->g == 255 ||
	     actives->b == 255))
		state = STATE_BLINK_DOWN;

	else if ((actives->r == 0 && reference.r != 0) ||
	    (actives->g == 0 && reference.g != 0) ||
	    (actives->b == 0 && reference.b != 0))
		state = STATE_BLINK_UP;

	rmt_enqueue(actives, ACTIVE_SIZE);
	return SCHED_CONTINUE;
}

esp_err_t
led_blink(uint8_t color)
{
	struct pixel	p;
	uint64_t       *newepoch;

	if (color >= LED_COLOR_MAX) CATCH_RETURN(EINVAL);
	else p = color_to_pixel(color);

	/* knock out the previous effect */
	CATCH_RETURN(epoch_new(&newepoch));

	memset(actives, 0, ACTIVE_SIZE);
	CATCH_RETURN(rmt_enqueue(actives, ACTIVE_SIZE));
	CATCH_RETURN(sched_schedule(SCHED_US_PER_S / ANIMATION_FPS,
	    blink_cb, newepoch));

	state = STATE_BLINK_UP;
	reference = p;
	return 0;
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
		if (packed[primaryidx] == 255) state = STATE_SPIN;
	}
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
		ESP_LOGW(TAG, "transition point -> %d", i);
		transitionpt = i;
	}
}


static int
spin_cb(void *arg)
{
	uint64_t	*myepoch = (uint64_t *)arg;

	if (*myepoch != epoch) {
		free(myepoch);
		return SCHED_STOP;
	}

	if (state == STATE_SPIN) update_spin();
	else prep_spin();

	rmt_enqueue(actives, ACTIVE_SIZE);
	return SCHED_CONTINUE;
}

esp_err_t
led_spin(uint8_t color)
{
	uint64_t	*newepoch;
	struct pixel	 p;

	if (color >= LED_COLOR_MAX) CATCH_RETURN(EINVAL);
	else p = color_to_pixel(color);

	/* short circuit if we're already the correct color */
	if (p.r == reference.r && p.g == reference.g && p.b == reference.b)
		if (state == STATE_SPIN_UP || state == STATE_SPIN)
			goto end;

	/* knock out the previous effect */
	CATCH_RETURN(epoch_new(&newepoch));
	update_indices(p);

	memset(actives, 0, ACTIVE_SIZE);
	CATCH_RETURN(rmt_enqueue(actives, ACTIVE_SIZE));
	CATCH_RETURN(sched_schedule(SCHED_US_PER_S / ANIMATION_FPS, spin_cb, newepoch));

	reference = p;
	state = STATE_SPIN_UP;
	transitionpt = 0;

 end:
	return 0;
}