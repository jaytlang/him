/* fs.c
 * utilities for setting up the SPIFFS filesystem
 * onboard the device. once these initialization routines
 * run, we can hit the files stored here using normal POSIX APIs
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
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "esp_spiffs.h"
#include "esp_log.h"
#include "nvs_flash.h"

#include "him.h"

LOG_SET_TAG("fs");

esp_err_t
fs_init(void)
{
	esp_vfs_spiffs_conf_t cfg = { 0 };

	ESP_LOGI(TAG, "initializing filesystem");
	CATCH_RETURN(nvs_flash_init());

	cfg.base_path = FS_ROOT;
	cfg.max_files = 10;
	CATCH_RETURN(esp_vfs_spiffs_register(&cfg));

	ESP_LOGI(TAG, "mounted filesystem, running fsck..");
	CATCH_RETURN(esp_spiffs_check(NULL));
	ESP_LOGI(TAG, "fsck ok");

	return 0;
}

esp_err_t
fs_slurp(char *fname, char **out, size_t *outsize)
{
	FILE		*f;
	esp_err_t	 rv = ESP_OK;

	if ((f = fopen(fname, "r")) == NULL) {
		if (errno == ENOENT) {
			ESP_LOGW(TAG, "file %s not found", fname);
			*out = NULL;
			*outsize = 0;
			goto end;
		}

		ESP_LOGW(TAG, "unknown filesystem error happened");
		CATCH_RETURN(errno);
	}

	ESP_LOGI(TAG, "opened %s, now reading", fname);

	fseek(f, 0, SEEK_END);
	*outsize = ftell(f);
	fseek(f, 0, SEEK_SET);

	if ((*out = calloc(*outsize, sizeof(char))) == NULL ||
	    fread(*out, *outsize, sizeof(char), f) != *outsize) {
		ESP_LOGI(TAG, "only read %zu bytes from file", *outsize);
		rv = errno;
	}

	fclose(f);
end:
	return rv;
}

esp_err_t
fs_write_credentials(char *ssid, char *pass)
{
	FILE	*f;

	if ((f = fopen(FS_SSIDPATH, "w+")) == NULL)
		CATCH_RETURN(errno);

	else if (fwrite(ssid, sizeof(char), strlen(ssid) + 1, f) !=
	    strlen(ssid) + 1) {
		fclose(f);
		remove(FS_SSIDPATH);
		CATCH_RETURN(errno);
	}

	fclose(f);

	if (pass != NULL) {
		if ((f = fopen(FS_PASSPATH, "w+")) == NULL) {
			remove(FS_SSIDPATH);
			CATCH_RETURN(errno);

		} else if (fwrite(pass, sizeof(char), strlen(pass) + 1, f)
		    != strlen(pass) + 1) {
			fclose(f);
			remove(FS_SSIDPATH);
			remove(FS_PASSPATH);
			CATCH_RETURN(errno);
		}

		fclose(f);
	}

	ESP_LOGI(TAG, "credentials updated to %s/%s",
	    ssid, (pass == NULL) ? "null" : pass);

	return 0;
}

esp_err_t
fs_read_credentials(char **ssid, char **pass)
{
	size_t	 bufsize;

	CATCH_DIE(fs_slurp(FS_SSIDPATH, ssid, &bufsize));
	CATCH_DIE(fs_slurp(FS_PASSPATH, pass, &bufsize));

	if (*ssid != NULL)
		ESP_LOGI(TAG, "credentials read as %s/%s", *ssid,
		    (*pass == NULL) ? "null" : *pass);

	else ESP_LOGI(TAG, "no credentials found");

	return 0;
}

void
fs_clear_credentials(void)
{
	remove(FS_SSIDPATH);
	remove(FS_PASSPATH);
}