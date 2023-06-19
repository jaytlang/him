/* main.c
 * himmnmnmnmnmnnmnmnnnm
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
#include <stdio.h>
#include <unistd.h>

#include <FreeRTOS/FreeRTOS.h>
#include <FreeRTOS/task.h>

#include "esp_event.h"
#include "esp_log.h"
#include "esp_system.h"

#include "him.h"

LOG_SET_TAG("main");

static void	 wifi_state_changed(int);
static char	*ssid, *pass;
static int	 dying = 0;

static void
wifi_state_changed(int connected)
{
	TaskHandle_t	h = NULL;
	if (!connected) {
		ESP_LOGW(TAG, "lost wifi connectivity");
		fs_clear_credentials();
		reboot(NULL);
	}

	ESP_LOGI(TAG, "wifi up, starting late boot activities");
	CATCH_DIE(app_init());
	CATCH_DIE(button_init(1));

	/* never returns */
	xTaskCreate(app_readloop, "read loop", 4096, NULL, tskIDLE_PRIORITY, &h);
}

void
die(void)
{
	ESP_LOGW(TAG, "unrecoverable error detected; resetting");
	dying = 1;

	fs_clear_credentials();
	reboot(NULL);
}

int
reboot(void *arg)
{
	ESP_LOGW(TAG, "preparing for reboot");

	if (!dying) {
		if (ssid == NULL) httpd_teardown();
		led_teardown();
		rmt_teardown();
		button_teardown();
	}

	esp_restart();

	/* never reached */
	return SCHED_STOP;
	(void)arg;
}

void
app_main(void)
{
	/* mission critical */
	ESP_LOGI(TAG, "hello, world!");
	esp_log_level_set("*", LOG_LEVEL);
	esp_event_loop_create_default();

	/* early boot */
	CATCH_GOTO(rmt_init(), error);
	CATCH_GOTO(led_init(), error);

	CATCH_GOTO(led_blink(LED_COLOR_RED), error);

	/* late boot */
	CATCH_GOTO(fs_init(), error);
	CATCH_GOTO(fs_read_credentials(&ssid, &pass), error);

	if (ssid == NULL) {
		ESP_LOGI(TAG, "no credentials found, bootstrapping");
		CATCH_GOTO(led_blink(LED_COLOR_YELLOW), error);

		CATCH_GOTO(wifi_initap(), error);
		CATCH_GOTO(mdns_advertise(), error);
		CATCH_GOTO(httpd_init(), error);
		CATCH_GOTO(button_init(0), error);

	} else {
		ESP_LOGI(TAG, "got credentials, connecting (cb = %p)", &wifi_state_changed);
		CATCH_GOTO(wifi_initsta(ssid, pass, &wifi_state_changed), error);
	}

	return;
error:
	die();
}

