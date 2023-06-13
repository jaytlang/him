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

#include "esp_event.h"
#include "esp_log.h"
#include "esp_system.h"

#include "him.h"

LOG_SET_TAG("main");

void
die(void)
{
	fs_clear_credentials();
	httpd_teardown();
	rmt_teardown();

	ESP_LOGW(TAG, "unrecoverable error detected; rebooting");
	sleep(5);
	esp_restart();
}

int
reboot(void)
{
	ESP_LOGI(TAG, "preparing for reboot");
	httpd_teardown();
	rmt_teardown();
	esp_restart();

	/* never reached */
	return SCHED_STOP;
}

void
app_main(void)
{
	char		*ssid, *pass;

	/* early boot */
	ESP_LOGI(TAG, "hello, world!");
	esp_log_level_set("*", LOG_LEVEL);

	CATCH_GOTO(esp_event_loop_create_default(), error);
	CATCH_GOTO(rmt_init(), error);
	CATCH_GOTO(led_init(), error);

	CATCH_GOTO(led_blink(LED_COLOR_PURPLE), error);
	/* the LED blink interval is 1 second - get on and off */
	sleep(2);
	CATCH_GOTO(led_solid(0), error);

	/* late boot */

	CATCH_GOTO(fs_init(), error);
	CATCH_GOTO(fs_read_credentials(&ssid, &pass), error);

	if (ssid == NULL) {
		ESP_LOGI(TAG, "no credentials found, bootstrapping");
		CATCH_GOTO(led_blink(LED_COLOR_RED), error);

		CATCH_GOTO(wifi_initap(), error);
		CATCH_GOTO(mdns_advertise(), error);
		CATCH_GOTO(httpd_init(), error);

	} else {
		ESP_LOGI(TAG, "got credentials, connecting");
		CATCH_GOTO(led_blink(LED_COLOR_TURQUOISE), error);
		CATCH_GOTO(wifi_initsta(ssid, pass), error);
	}

	return;
error:
	die();
}

