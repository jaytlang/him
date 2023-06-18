/* httpd.c
 * our web server for setting himmnmnnmn up
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

#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "esp_http_server.h"
#include "esp_log.h"

#include "him.h"

LOG_SET_TAG("httpd");

static httpd_handle_t	 server = NULL;
static httpd_uri_t	 uris[HTTPD_MAXURI];
static size_t		 numuris = 0;

static char		*get_query_string(httpd_req_t *);
static char		*query_string_lookup(char *, char *);

static esp_err_t	 handle_file(httpd_req_t *);
static esp_err_t	 handle_cred_update(httpd_req_t *);

static char *
get_query_string(httpd_req_t *req)
{
	char	*str;
	size_t	 strsize;

	strsize = httpd_req_get_url_query_len(req);

	if (strsize == 0) return NULL;
	else if ((str = calloc(++strsize, sizeof(char))) == NULL)
		CATCH_DIE(errno);

	CATCH_DIE(httpd_req_get_url_query_str(req, str, strsize));
	return str;
}

static char *
query_string_lookup(char *str, char *key)
{
	char		*encbuf = NULL, *buf = NULL;
	size_t		 i, j, bufsize;
	esp_err_t	 err;

	/* overkill */
	bufsize = strlen(str) + 1;

	if ((encbuf = malloc(bufsize)) == NULL) CATCH_DIE(errno);
	else if ((buf = malloc(bufsize)) == NULL) CATCH_DIE(errno);

	err = httpd_query_key_value(str, key, encbuf, bufsize);

	if (err != ESP_OK) {
		if (buf != NULL) free(buf);
		if (encbuf != NULL) free(encbuf);
		return NULL;
	}

	/* URL decode
	 * i = dstbuf index, j = srcbuf index
	 */
	for (i = 0, j = i; j < bufsize;) {
		char a, b;

		if (encbuf[j] == '%' &&
		    (a = encbuf[j + 1]) &&
		    (b = encbuf[j + 2]) &&
		    isxdigit(a) && isxdigit(b)) {

		    	if (a >= 'a') a -= 'a' - 'A';
			if (a >= 'A') a -= ('A' - 10);
			else a -= '0';

			if (b >= 'a') b -= 'a' - 'A';
			if (b >= 'A') b -= ('A' - 10);
			else b -= '0';

			buf[i++] = 16 * a + b;
			j += 3;

		} else if (encbuf[j] == '+') {
			buf[i++] = ' ';
			j++;

		} else buf[i++] = encbuf[j++];
	}

	free(encbuf);
	return buf;
}

static esp_err_t
handle_file(httpd_req_t *req)
{
	char	*body, *fname;
	char	*suffix, *type;
	size_t 	 bodysize, fnamesize;

	/* fix '/' -> '/index.html' */
	if (strlen(req->uri) == 1) strcpy((char *)req->uri, HTTPD_INDEX);

	/* find file suffix and set content type accordingly */
	for (suffix = (char *)req->uri + 1; *(suffix - 1) != '.'; suffix++);

	if (strcmp(suffix, "css") == 0) type = "text/css";
	else if (strcmp(suffix, "woff2") == 0) type = "font/woff2";
	else if (strcmp(suffix, "ico") == 0) type = "image/vnd.microsoft.icon";
	else type = "text/html";

	CATCH_DIE(httpd_resp_set_type(req, type));
	ESP_LOGI(TAG, "suffix %s => mime type %s", suffix, type);

	/* in case we are handling HTTPD_CONFIRM, knock out any query
	 * characters that will mess with the filesystem
	 */
	for (; *suffix != '\0'; suffix++) {
		if (*suffix == '?') {
			*suffix = '\0';
			break;
		}
	}

	fnamesize = strlen(req->uri) + strlen(HTTPD_ROOT) + 1;
	if ((fname = calloc(fnamesize, sizeof(char))) == NULL)
		CATCH_DIE(errno);

	strcpy(fname, HTTPD_ROOT);
	strcat(fname, req->uri);

	ESP_LOGI(TAG, "requested file %s", fname);

	CATCH_DIE(fs_slurp((char *)fname, &body, &bodysize));
	CATCH_DIE(httpd_resp_send(req, body, (ssize_t)bodysize));

	ESP_LOGI(TAG, "retrieved file with size = %u", bodysize);

	free(fname);
	free(body);
	return 0;
}

static esp_err_t
handle_cred_update(httpd_req_t *req)
{
	char	*buf, *ssid, *pass;

	if ((buf = get_query_string(req)) == NULL)
		return ESP_FAIL;

	ssid = query_string_lookup(buf, HTTPD_SSID_KEY);
	if (ssid == NULL) {
		free(buf);
		return ESP_FAIL;
	}

	pass = query_string_lookup(buf, HTTPD_PASS_KEY);
	CATCH_DIE(fs_write_credentials(ssid, pass));

	free(ssid);
	if (pass != NULL) free(pass);
	free(buf);

	ESP_LOGI(TAG, "credential update complete, scheduling reboot in 10s");
	sched_schedule(10 * SCHED_US_PER_S, reboot, NULL);
	return handle_file(req);
}

esp_err_t
httpd_init(void)
{
	DIR		*dp;
	struct dirent	*entry;

	httpd_config_t	 cfg = HTTPD_DEFAULT_CONFIG();
	esp_err_t	 rv = ESP_OK;

	CATCH_RETURN(httpd_start(&server, &cfg));
	ESP_LOGI(TAG, "started http server");

	if ((dp = opendir(HTTPD_ROOT)) == NULL) {
		rv = errno;
		CATCH_GOTO(errno, fail);
	}

	for (numuris = 0; (entry = readdir(dp)) != NULL;) {
		char	*path;

		if (strcmp(entry->d_name, HTTPD_CONFIRM + 1) == 0) continue;

		path = calloc(strlen(entry->d_name) + 2, sizeof(char));
		if (path == NULL) {
			rv = errno;
			CATCH_GOTO(errno, fail);
		}

		path[0] = '/';
		strcpy(path + 1, entry->d_name);

		uris[numuris].uri = path;
		uris[numuris].method = HTTP_GET;
		uris[numuris].handler = &handle_file;
		uris[numuris].user_ctx = NULL;

		ESP_LOGI(TAG, "registered file handler %s (%p)", path, path);

		CATCH_GOTO(rv, fail);
		rv = httpd_register_uri_handler(server, &uris[numuris++]);
	}

	/* two more for the special handlers */
	uris[numuris].uri = HTTPD_CONFIRM;
	uris[numuris].method = HTTP_GET;
	uris[numuris].handler = &handle_cred_update;
	uris[numuris].user_ctx = NULL;

	rv = httpd_register_uri_handler(server, &uris[numuris++]);
	if (rv == ESP_OK)
		ESP_LOGI(TAG, "registered special handler " HTTPD_CONFIRM);

	uris[numuris].uri = "/";
	uris[numuris].method = HTTP_GET;
	uris[numuris].handler = &handle_file;
	uris[numuris].user_ctx = NULL;

	rv = httpd_register_uri_handler(server, &uris[numuris++]);
	if (rv == ESP_OK)
		ESP_LOGI(TAG, "registered special handler /");


fail:
	if (rv != ESP_OK) {
		httpd_stop(server);
		server = NULL;

		if (numuris > 0) {
			size_t i;

			for (i = 0; i < numuris; i++)
				if (strcmp(uris[i].uri, HTTPD_CONFIRM) != 0 &&
				    strlen(uris[i].uri) != 1)
					free((char *)uris[i].uri);

			numuris = 0;
		}
	}

	return rv;
}

void
httpd_teardown(void)
{
	size_t	i;

	if (server != NULL) {
		ESP_LOGI(TAG, "stopping http server");
		httpd_stop(server);
		server = NULL;

		for (i = 0; i < numuris; i++) {
			ESP_LOGI(TAG, "unregistering uri %p", uris[i].uri);
			if (strcmp(uris[i].uri, HTTPD_CONFIRM) != 0 &&
			    strlen(uris[i].uri) != 1)
				free((char *)(uris[i].uri));
		}

		numuris = 0;
	}
}
