/* rmt.c
 * handles the low-level encoding of ws2812 byte streams
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

#include "driver/rmt_encoder.h"
#include "driver/rmt_tx.h"
#include "esp_attr.h"
#include "esp_log.h"

#include "him.h"

#define HZ_PER_MHZ		1000000
#define NS_PER_S		1000000000

#define TICK_PERIOD_NS		100
#define TICK_FREQUENCY_HZ	(NS_PER_S / TICK_PERIOD_NS)

/* each symbol is a pair of 16 bit values - so in all
 * that's 32 bits -> 4 bytes to hold a single WS2812 'bit'.
 * our protocol is doing 8 bit color, so that's 24 * 4 = 96
 * bytes per LED. 96 * 16 = 1536 to hold an entire transmission
 * for 16 LEDs - that's a lot, but maybe we can do it?
 */
#define SYMBOL_BYTES		4
#define MEM_SYMBOLS		(SYMBOL_BYTES * RMT_BITS_PER_LED * RMT_NUM_LEDS)

/* totally arbitrary pick here */
#define TXQ_BACKLOG_SIZE	4

/* WS2812 protocol - each bit is a pair of levels
 * The duration of each level sums to 1200ns, and
 * determines whether the bit is a one or a zero
 */
#define LEVEL_FIRST		1
#define LEVEL_SECOND		0

#define RESET_NS		50000
#define ZERO_LEVEL_FIRST_NS	300
#define ZERO_LEVEL_SECOND_NS	900
#define ONE_LEVEL_FIRST_NS	900
#define ONE_LEVEL_SECOND_NS	300

#define NS_TO_TICKS(NS)		((NS) / TICK_PERIOD_NS)	

LOG_SET_TAG("rmt");

static IRAM_ATTR size_t	rmt_encode(rmt_encoder_t *, rmt_channel_handle_t,
	    		    const void *, size_t, rmt_encode_state_t *);
static esp_err_t	rmt_reset(rmt_encoder_t *);
static esp_err_t	rmt_del(rmt_encoder_t *);

static DRAM_ATTR rmt_channel_handle_t	  chan = NULL;
static DRAM_ATTR rmt_encoder_t	 	  base;
static DRAM_ATTR rmt_encoder_t		 *dataobj, *blankobj;
static DRAM_ATTR rmt_symbol_word_t	  blankdata;
static DRAM_ATTR int		 	  blanking;

static size_t
rmt_encode(rmt_encoder_t *enc, rmt_channel_handle_t ch,
    const void *data, size_t datasize, rmt_encode_state_t *stateout)
{
	/* the RMT encoding API is non-blocking - basically, we
	 * call encode() and get a result which is one or more of
	 * - complete
	 * - would block (i.e. "memory full")
	 * 
	 * in addition, there's also this RESET flag, which
	 * indicates that the encoder is 'reset' and ready for
	 * another transmission. in reality, however, this is
	 * just defined to zero, so we can not use it
	 */
	size_t			written = 0;
	rmt_encode_state_t	substate;

	if (!blanking) {
		written = dataobj->encode(dataobj, chan,
		    data, datasize, &substate);

		if (substate & RMT_ENCODING_COMPLETE) blanking = 1;
		if (substate & RMT_ENCODING_MEM_FULL) {
			*stateout = RMT_ENCODING_MEM_FULL;
			goto end;
		}
	}

	if (blanking) {
		written += blankobj->encode(blankobj, chan,
		    &blankdata, sizeof(rmt_symbol_word_t), stateout);
		if (*stateout & RMT_ENCODING_COMPLETE) blanking = 0;
	}

end:
	return written;
	(void)enc;
	(void)ch;
}

static esp_err_t
rmt_reset(rmt_encoder_t *enc)
{
	CATCH_RETURN(rmt_encoder_reset(blankobj));
	CATCH_RETURN(rmt_encoder_reset(dataobj));
	blanking = 0;

	return 0;
	(void)enc;
}

static esp_err_t
rmt_del(rmt_encoder_t *enc)
{
	CATCH_RETURN(rmt_reset(enc));
	CATCH_RETURN(rmt_del_encoder(blankobj));
	CATCH_RETURN(rmt_del_encoder(dataobj));

	return 0;
	(void)enc;
}

esp_err_t
rmt_init(void)
{
	rmt_tx_channel_config_t cfg = { 0 };
	rmt_copy_encoder_config_t blankcfg;
	rmt_bytes_encoder_config_t datacfg;

	/* set up tx channel */
	cfg.gpio_num = RMT_GPIO_NUM;
	cfg.clk_src = RMT_CLK_SRC_APB;
	cfg.resolution_hz = TICK_FREQUENCY_HZ;
	cfg.mem_block_symbols = MEM_SYMBOLS;
	cfg.trans_queue_depth = TXQ_BACKLOG_SIZE;
	cfg.flags.with_dma = 1;

	CATCH_RETURN(rmt_new_tx_channel(&cfg, &chan));
	CATCH_RETURN(rmt_enable(chan));

	/* reset signal */
	CATCH_RETURN(rmt_new_copy_encoder(&blankcfg,
	    (rmt_encoder_handle_t *)&blankobj));

	blankdata.level0 = 0;
	blankdata.duration0 = NS_TO_TICKS(RESET_NS) / 2;
	blankdata.level1 = 0;
	blankdata.duration1 = NS_TO_TICKS(RESET_NS) / 2;
	blanking = 0;

	/* data stuff */
	datacfg.bit0.level0 = datacfg.bit1.level0 = LEVEL_FIRST;
	datacfg.bit0.level1 = datacfg.bit1.level1 = LEVEL_SECOND;
	datacfg.bit0.duration0 = NS_TO_TICKS(ZERO_LEVEL_FIRST_NS);
	datacfg.bit0.duration1 = NS_TO_TICKS(ZERO_LEVEL_SECOND_NS);
	datacfg.bit1.duration0 = NS_TO_TICKS(ONE_LEVEL_FIRST_NS);
	datacfg.bit1.duration1 = NS_TO_TICKS(ONE_LEVEL_SECOND_NS);
	datacfg.flags.msb_first = 1;

	CATCH_RETURN(rmt_new_bytes_encoder(&datacfg,
	    (rmt_encoder_handle_t *)&dataobj));

	/* plumbing */
	base.encode = &rmt_encode;
	base.reset = &rmt_reset;
	base.del = &rmt_del;

	return 0;
}

esp_err_t
rmt_enqueue(void *data, size_t datasize)
{
	rmt_transmit_config_t cfg = { 0 };

	CATCH_RETURN(rmt_transmit(chan, &base, data, datasize, &cfg));
	return 0;
}

void
rmt_teardown(void)
{
	rmt_del_channel(chan);
	chan = NULL;
}                                                     