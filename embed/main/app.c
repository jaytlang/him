/* app.c
 * main application logic
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
#include <sys/socket.h>

#include <arpa/inet.h>
#include <netinet/in.h>

#include <stdint.h>
#include <errno.h>
#include <netdb.h>
#include <stdlib.h>
#include <unistd.h>

#include "him.h"

LOG_SET_TAG("app");

static int	sockfd = -1;

esp_err_t
app_init(void)
{
	struct hostent		*host;
	struct sockaddr_in	 sa;

	sockfd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
	if (sockfd < 0) CATCH_RETURN(errno);

	if ((host = gethostbyname(APP_NAME)) == NULL) {
		/* may not provide a good report, not sure */
		close(sockfd);
		CATCH_RETURN(h_errno);
	}

	ESP_LOGI(TAG, "connecting to %s", inet_ntoa(*(in_addr_t *)host->h_addr));

	sa.sin_family = AF_INET;
	sa.sin_port = htons(APP_PORT);
	sa.sin_addr.s_addr = *(in_addr_t *)host->h_addr;

	if (connect(sockfd, (struct sockaddr *)&sa,
	    sizeof(struct sockaddr_in)) < 0) {
		close(sockfd);
		CATCH_RETURN(errno);
	}

	return 0;
}

void
app_readloop(void *arg)
{
	for (;;) {
		ssize_t	bytesread;
		uint8_t	newcolor;

		bytesread = read(sockfd, &newcolor, sizeof(uint8_t));
		if (bytesread < 0) CATCH_DIE(errno);
		else if (bytesread == 0) {
			/* try a quick reboot, might be okay
			 * on reboot, if we can't connect, _then_ die
			 */
			ESP_LOGI(TAG, "server closed connection");
			reboot(NULL);
		}

		ESP_LOGI(TAG, "changing color to %d", newcolor);
		led_spin(newcolor);
	}

	(void)arg;
}

esp_err_t
app_changecolor(void)
{
	uint8_t	newcolor;
	ssize_t	byteswritten;

	newcolor = led_currentcolor() + 1;
	if (newcolor >= LED_COLOR_MAX) newcolor = 1;

	ESP_LOGI(TAG, "telling the server about candidate new color %d", newcolor);
	byteswritten = write(sockfd, &newcolor, sizeof(uint8_t));

	if (byteswritten < 0) CATCH_DIE(errno);
	else if (byteswritten == 0) {
		/* try a quick reboot, might be okay
		 * on reboot, if we can't connect, _then_ die
		 */
		ESP_LOGI(TAG, "server closed connection");
		reboot(NULL);
	}

	return 0;
}