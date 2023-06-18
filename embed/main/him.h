/* him.h
 * basically all of our 'exported' functions
 * from each file live here
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

#ifndef HIM_H
#define HIM_H

#include "esp_event.h"
#include "esp_check.h"

/* esp error checking */
#define CATCH_GOTO(X, LABEL) do {		\
	char errbuf[128];			\
	esp_err_t ret;				\
						\
	ret = X;				\
	esp_err_to_name_r(ret, errbuf, 1024);	\
	ESP_GOTO_ON_ERROR(ret, LABEL, TAG,	\
		"%s", errbuf);			\
} while (0)

#define CATCH_RETURN(X) do {			\
	char errbuf[128];			\
	esp_err_t ret;				\
						\
	ret = X;				\
	esp_err_to_name_r(ret, errbuf, 1024);	\
	ESP_RETURN_ON_ERROR(ret, TAG,		\
		"%s", errbuf);			\
} while (0)

#define CATCH_DIE(X) do {				\
	esp_err_t ret;					\
	ret = X;					\
	if (ret != ESP_OK) {				\
		ESP_ERROR_CHECK_WITHOUT_ABORT(ret);	\
		die();					\
	}						\
} while (0)

/* logging setting - set this to ESP_LOG_VERBOSE
 * to enable debugging outputs. when this lands in
 * juliana's hands, set to ESP_LOG_WARN.
 */
#define LOG_LEVEL	ESP_LOG_WARN
#define LOG_SET_TAG(X)	static const char *TAG = X


/* main.c */
void			die(void);
int			reboot(void *);

/* fs.c */
#define FS_ROOT		"/spiffs"
#define FS_SSIDPATH	FS_ROOT "/credentials/ssid"
#define FS_PASSPATH	FS_ROOT "/credentials/pass"

esp_err_t		fs_init(void);
esp_err_t		fs_slurp(char *, char **, size_t *);
esp_err_t		fs_write_credentials(char *, char *);
esp_err_t		fs_read_credentials(char **, char **);
void			fs_clear_credentials(void);

/* wifi.c */
#define WIFI_SSID	"Him"

#define WIFI_AP_IP	"10.0.0.1"
#define WIFI_AP_GATEWAY	WIFI_AP_IP
#define WIFI_AP_NETMASK	"255.0.0.0"

#define WIFI_RETRIES	5

esp_err_t		wifi_initap(void);
esp_err_t		wifi_initsta(char *, char *, void (*)(int));

/* mdns.c */
#define MDNS_HOSTNAME	"him"
#define MDNS_INSTANCE	"Setting Him Up"

esp_err_t		mdns_advertise(void);

/* httpd.c */
#define HTTPD_ROOT	FS_ROOT "/web"
#define HTTPD_INDEX	"/index.html"
#define HTTPD_CONFIRM	"/confirm.html"

#define HTTPD_SSID_KEY	"ssid"
#define HTTPD_PASS_KEY	"password"

#define HTTPD_MAXURI	10

esp_err_t		httpd_init(void);
void			httpd_teardown(void);

/* sched.c */
#define SCHED_CONTINUE	0
#define SCHED_STOP	1

#define SCHED_US_PER_MS	1000
#define SCHED_MS_PER_S	1000
#define SCHED_US_PER_S	(SCHED_US_PER_MS * SCHED_MS_PER_S)

esp_err_t		sched_schedule(uint64_t, int (*)(void *), void *);

/* rmt.c */
#define RMT_GPIO_NUM		4
#define RMT_NUM_LEDS		16

/* we're not built to parameterize around these
 * values, we assume that they maintain these values.
 * such assumptions are inherent in e.g. the choice
 * of char arrays in led.c to encode colors
 * so, don't change the below defines
 */
#define RMT_COLOR_BITS		8
#define RMT_BITS_PER_LED	(RMT_COLOR_BITS * 3)
#define RMT_BYTES_PER_LED	(RMT_BITS_PER_LED / 8)

esp_err_t		rmt_init(void);
void			rmt_teardown(void);
esp_err_t		rmt_enqueue(void *, size_t);

/* led.c */
#define LED_COLOR_RED		1
#define LED_COLOR_GREEN		2
#define LED_COLOR_BLUE		4

#define LED_COLOR_YELLOW	(LED_COLOR_RED | LED_COLOR_GREEN)
#define LED_COLOR_TURQUOISE	(LED_COLOR_GREEN | LED_COLOR_BLUE)
#define LED_COLOR_PURPLE	(LED_COLOR_BLUE | LED_COLOR_RED)

#define LED_COLOR_MAX		(LED_COLOR_RED | \
				 LED_COLOR_GREEN | \
				 LED_COLOR_BLUE)

esp_err_t		led_init(void);
uint8_t			led_currentcolor(void);
void			led_teardown(void);

/* could definitely have some sort of cool callback-based
 * design here where you get a callback once every 1/60s
 * to adjust a pixel as you see fit
 * but that sounds like it would take time and my ritalin
 * only stays in effect for so long (:
 */
esp_err_t		led_solid(uint8_t);
esp_err_t		led_blink(uint8_t);
esp_err_t		led_spin(uint8_t);

/* button.c */
#define BUTTON_GPIO_NUM		6
#define BUTTON_EVENT_CHANGED	1

#define BUTTON_RESET_DELAY_US	(3 * SCHED_US_PER_S)

ESP_EVENT_DECLARE_BASE(BUTTON_EVENT);

esp_err_t		button_init(int);
void			button_teardown(void);

/* app.c */
#define APP_NAME	"juliana.jtlang.dev"
#define APP_PORT	6969

esp_err_t		app_init(void);
void			app_readloop(void *);
esp_err_t		app_changecolor(void);

#endif /* HIM_H */
