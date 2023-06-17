/* wifi.c
 * routines for access point configuration during initial
 * bootstrap, and later on acting as a proper client
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
#include <string.h>

#include "esp_event.h"
#include "esp_netif.h"
#include "esp_log.h"
#include "esp_wifi.h"

#include "him.h"

LOG_SET_TAG("wifi");

static esp_netif_t	*netif;

static esp_err_t	 netif_init_ap(void);
static esp_err_t	 netif_init_sta(void);

static void		 wifi_event_handler(void *, esp_event_base_t,
			    int32_t, void *);

static void		(*cb)(int);

static esp_err_t
netif_init_ap(void)
{
	esp_netif_ip_info_t	ipinfo = { 0 };

	CATCH_RETURN(esp_netif_init());
	netif = esp_netif_create_default_wifi_ap();

	/* esp-idf likes to kick off DHCP absolutely literally
	 * immediately, but we need to configure its' internal
	 * structures. so stop it, configure it, and then restart
	 */
	CATCH_RETURN(esp_netif_dhcps_stop(netif));

	ipinfo.ip.addr = esp_ip4addr_aton(WIFI_AP_IP);
	ipinfo.netmask.addr = esp_ip4addr_aton(WIFI_AP_NETMASK);
	ipinfo.gw.addr = esp_ip4addr_aton(WIFI_AP_GATEWAY);

	CATCH_RETURN(esp_netif_set_ip_info(netif, &ipinfo));
	CATCH_RETURN(esp_netif_dhcps_start(netif));

	return 0;
}

static esp_err_t
netif_init_sta(void)
{
	CATCH_RETURN(esp_netif_init());
	netif = esp_netif_create_default_wifi_sta();
	return 0;
}

static void
wifi_event_handler(void *arg, esp_event_base_t base, int32_t id, void *data)
{
	if (base == WIFI_EVENT) {
		if (id == WIFI_EVENT_STA_START) {
			ESP_LOGI(TAG, "wifi up, attempting to connect to AP");
			esp_wifi_connect();
		} else if (id == WIFI_EVENT_STA_DISCONNECTED) {
			ESP_LOGW(TAG, "unable to connect to AP");
			cb(0);
		}
	} else if (base == IP_EVENT) {
		if (id == IP_EVENT_STA_GOT_IP) {
			ESP_LOGI(TAG, "negotiated IP from AP, all good");
			ESP_LOGI(TAG, "calling %p", cb);
			cb(1);
		}
	}
}

esp_err_t
wifi_initap(void)
{
	wifi_init_config_t	initcfg = WIFI_INIT_CONFIG_DEFAULT();
	wifi_config_t		cfg;

	CATCH_RETURN(netif_init_ap());

	CATCH_RETURN(esp_wifi_init(&initcfg));
	CATCH_RETURN(esp_wifi_set_mode(WIFI_MODE_AP));

	CATCH_RETURN(esp_wifi_get_config(WIFI_IF_AP, &cfg));
	memcpy(cfg.ap.ssid, WIFI_SSID, strlen(WIFI_SSID) + 1);
	cfg.ap.ssid_len = 0;
	cfg.ap.authmode = WIFI_AUTH_OPEN;
	CATCH_RETURN(esp_wifi_set_config(WIFI_IF_AP, &cfg));

	CATCH_RETURN(esp_wifi_start());

	ESP_LOGI(TAG, "initialized AP with SSID %s", WIFI_SSID);
	return 0;
}

esp_err_t
wifi_initsta(char *ssid, char *pass, void (*ncb)(int))
{
	wifi_init_config_t		initcfg = WIFI_INIT_CONFIG_DEFAULT();
	wifi_config_t			cfg;

	CATCH_RETURN(netif_init_sta());

	CATCH_RETURN(esp_wifi_init(&initcfg));
	CATCH_RETURN(esp_wifi_set_mode(WIFI_MODE_STA));

	CATCH_RETURN(esp_wifi_get_config(WIFI_IF_STA, &cfg));
	memcpy(cfg.sta.ssid, ssid, strlen(ssid) + 1);

	if (pass == NULL) {
		*cfg.sta.password = '\0';
		cfg.sta.threshold.authmode = WIFI_AUTH_OPEN;
	} else {
		memcpy(cfg.sta.password, pass, strlen(pass) + 1);
		/* best guess... */
		cfg.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;
	}

	CATCH_RETURN(esp_wifi_set_config(WIFI_IF_STA, &cfg));

	cb = ncb;

	CATCH_RETURN(esp_event_handler_register(WIFI_EVENT,
	    WIFI_EVENT_STA_START,
	    &wifi_event_handler,
	    NULL));
	CATCH_RETURN(esp_event_handler_register(WIFI_EVENT,
	    WIFI_EVENT_STA_DISCONNECTED,
	    &wifi_event_handler,
	    NULL));
	CATCH_RETURN(esp_event_handler_register(IP_EVENT,
	    IP_EVENT_STA_GOT_IP,
	    &wifi_event_handler,
	    NULL));

	CATCH_RETURN(esp_wifi_start());

	if (pass == NULL)
		ESP_LOGI(TAG, "initialized STA with open SSID %s", ssid);
	else ESP_LOGI(TAG, "intialized STA with SSID %s, WPA2-PSK password %s",
	    ssid, pass);

	return 0;
}