/* button.c
 * gpio thingo. enqueues events to the main event loop
 * whenever we press a button
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
#include <unistd.h>

#include "driver/gpio.h"
#include "driver/gpio_filter.h"
#include "esp_event.h"
#include "esp_intr_alloc.h"
#include "esp_log.h"

#include "him.h"

LOG_SET_TAG("button");
ESP_EVENT_DEFINE_BASE(BUTTON_EVENT);

static gpio_isr_handle_t		isr;
static gpio_glitch_filter_handle_t	filter;
static uint64_t				pressnumber = 0;

static int	buttonup = 0;
static int	enableshort = 0;

static IRAM_ATTR void	button_isr(void *);
static void		button_event_handler(void *, esp_event_base_t, int32_t, void *);
static int		reboot_checker(void *);

static int
reboot_checker(void *arg)
{
	uint64_t	oldpress = *(uint64_t *)arg;
	free(arg);

	if (oldpress == pressnumber) {
		ESP_LOGW(TAG, "user triggered voluntary reset");
		die();
	}

	return SCHED_STOP;
}

static void
button_event_handler(void *arg, esp_event_base_t base, int32_t id, void *data)
{
	uint64_t	*presses;
	int		 direction;

	if (base == BUTTON_EVENT && id == BUTTON_EVENT_CHANGED) {
		direction = !gpio_get_level(BUTTON_GPIO_NUM);

		if (direction) {
			presses = malloc(sizeof(uint64_t));
			if (presses == NULL) CATCH_DIE(errno);

			*presses = pressnumber;
			sched_schedule(BUTTON_RESET_DELAY_US, reboot_checker, presses);

		/* TODO - this is temporary */
		} else if (enableshort) app_changecolor();

		/* re-enable the button interrupt */
		CATCH_DIE(gpio_intr_enable(BUTTON_GPIO_NUM));
	}
}

static void
button_isr(void *arg)
{
	esp_event_isr_post(BUTTON_EVENT, BUTTON_EVENT_CHANGED, NULL, 0, NULL);
	/* briefly disable the button interrupt */
	CATCH_DIE(gpio_intr_disable(BUTTON_GPIO_NUM));
	(void)arg;
}

esp_err_t
button_init(int es)
{
	gpio_pin_glitch_filter_config_t	glitchcfg = { 0 };

	CATCH_RETURN(gpio_reset_pin(BUTTON_GPIO_NUM));
	CATCH_RETURN(gpio_set_direction(BUTTON_GPIO_NUM, GPIO_MODE_INPUT));
	CATCH_RETURN(gpio_set_pull_mode(BUTTON_GPIO_NUM, GPIO_PULLUP_ONLY));

	glitchcfg.gpio_num = BUTTON_GPIO_NUM;
	CATCH_RETURN(gpio_new_pin_glitch_filter(&glitchcfg, &filter));
	CATCH_GOTO(gpio_glitch_filter_enable(filter), freefilter);

	CATCH_GOTO(gpio_isr_register(button_isr, NULL, ESP_INTR_FLAG_LOWMED,
	    &isr), stopfilter);
	CATCH_GOTO(gpio_set_intr_type(BUTTON_GPIO_NUM, GPIO_INTR_ANYEDGE),
	    unregister);
	CATCH_GOTO(gpio_intr_enable(BUTTON_GPIO_NUM), unregister);

	CATCH_GOTO(esp_event_handler_register(BUTTON_EVENT,
	    BUTTON_EVENT_CHANGED,
	    &button_event_handler,
	    NULL), stopintr);

	buttonup = 1;
	enableshort = es;
	return 0;

stopintr:
	gpio_intr_disable(BUTTON_GPIO_NUM);
unregister:
	esp_intr_free(isr);
stopfilter:
	gpio_glitch_filter_disable(filter);
freefilter:
	gpio_del_glitch_filter(filter);

	return -1;
}

void
button_teardown(void)
{
	if (buttonup) {
		gpio_intr_disable(BUTTON_GPIO_NUM);
		esp_intr_free(isr);
		gpio_glitch_filter_disable(filter);
		gpio_del_glitch_filter(filter);
	}

	buttonup = 0;
	enableshort = 0;
}