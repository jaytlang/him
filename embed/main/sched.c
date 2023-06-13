/* sched.c
 * fire custom callbacks at deferred times - a
 * thin wrapper around the esp event library
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

#include "esp_log.h"
#include "esp_timer.h"
 
#include "him.h"

LOG_SET_TAG("sched");

struct cbargs {
	int			(*cb)(void);
	esp_timer_handle_t	  timer;
};

static void	sched_callback(void *);

void
sched_callback(void *arg)
{
	struct cbargs	*cba = (struct cbargs *)arg;
	if (cba->cb() == SCHED_STOP) {
		esp_timer_stop(cba->timer);
		esp_timer_delete(cba->timer);
		free(cba);
	}
}

esp_err_t
sched_schedule(uint64_t period, int (*cb)(void))
{
	struct cbargs		*cba;
	esp_timer_create_args_t	 cfg;	
	esp_err_t		 err;

	cba = malloc(sizeof(struct cbargs));
	if (cba == NULL) CATCH_RETURN(errno);

	cba->cb = cb;

	cfg.callback = sched_callback;
	cfg.arg = cba;
	cfg.dispatch_method = ESP_TIMER_TASK;
	cfg.name = "sched timer";
	cfg.skip_unhandled_events = 0;

	err = esp_timer_create(&cfg, &cba->timer);

	if (err != ESP_OK) {
		free(cba);
		CATCH_RETURN(err);
	}

	err = esp_timer_start_periodic(cba->timer, period);
	if (err != ESP_OK) {
		esp_timer_delete(cba->timer);
		free(cba);
		CATCH_RETURN(err);
	}

	return 0;
}
