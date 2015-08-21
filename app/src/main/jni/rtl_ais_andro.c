/*
 * Copyright (C) 2012 by Kyle Keen <keenerd@gmail.com>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 2 of the License, or
 * (at your option) any later version.
 *
 * Modification 2015 by Christian Ebner <cebner@gmx.at> www.ebc81.wordpress.com
 * based on the rtl_ais and rtl_tcp project
 * andoroid port based on rtl_tcp_andro from Martin Marinov
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */


/* todo
 * support left > right
 * thread left/right channels
 * more array sharing
 * something to correct for clock drift (look at demod's dc bias?)
 * 4x oversampling (with cic up/down)
 * droop correction
 * alsa integration
 * better upsampler (libsamplerate?)
 * windows support
 * ais decoder
 */

#include <errno.h>
#include <signal.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <unistd.h>
#ifdef WIN32
	#include <fcntl.h>
#endif

#include <arpa/inet.h>
#include <sys/socket.h>
#include <netinet/in.h>

#include <pthread.h>

#include <rtl-sdr.h>
//#include "convenience.h"
#include "aisdecoder/aisdecoder.h"

// For exit codes
#include "RtlAisJava.h"
#include <fcntl.h>
#include "rtl_ais_andro.h"
#include "librtlsdr_andro.h"
#include "rtl-sdr/src/convenience/convenience.h"

#define DEFAULT_ASYNC_BUF_NUMBER	12
#define DEFAULT_BUF_LENGTH		(16 * 16384)
#define AUTO_GAIN			-100
#define PPM_DURATION            10
#define PPM_DUMP_TIME           5
#define DEFAULT_SAMPLE_RATE     2048000



#define closesocket close
#define SOCKADDR struct sockaddr
#define SOCKET int
#define SOCKET_ERROR -1

static SOCKET s;
//static pthread_t tcp_worker_thread;
static pthread_t command_thread;

static pthread_t demod_thread;
static pthread_cond_t ready;
static pthread_mutex_t ready_m;
//static pthread_t ais_decoder_thread;

static volatile int do_exit = 0;
static volatile int is_running = 0;
static rtlsdr_dev_t *dev = NULL;

static pthread_mutex_t running_mutex = PTHREAD_MUTEX_INITIALIZER;

/* todo, less globals */
int16_t *merged;
int merged_len;
FILE *file=NULL;
int oversample = 0;
int dc_filter = 1;
int use_internal_aisdecoder=1;
int seconds_for_decoder_stats=0;
/* signals are not threadsafe by default */
#define safe_cond_signal(n, m) pthread_mutex_lock(m); pthread_cond_signal(n); pthread_mutex_unlock(m)
#define safe_cond_wait(n, m) pthread_mutex_lock(m); pthread_cond_wait(n, m); pthread_mutex_unlock(m)

struct downsample_state
{
	int16_t  *buf;
	int      len_in;
	int      len_out;
	int      rate_in;
	int      rate_out;
	int      downsample;
	int      downsample_passes;
	int16_t  lp_i_hist[10][6];
	int16_t  lp_q_hist[10][6];
	pthread_rwlock_t rw;
	//droop compensation
	int16_t  droop_i_hist[9];
	int16_t  droop_q_hist[9];

};

struct demod_state
{
	int16_t  *buf;
	int      buf_len;
	int16_t  *result;
	int      result_len;
	int      now_r, now_j;
	int      pre_r, pre_j;
	int      dc_avg;  // really should get its own struct

};

struct upsample_stereo
{
	int16_t *buf_left;
	int16_t *buf_right;
	int16_t *result;
	int     bl_len;
	int     br_len;
	int     result_len;
	int     rate;
};

struct init_ais_decoder_data{
   char * host;
   char * port;
   int show_levels;
   int debug_nmea;
   int buf_len;
   int time_print_stats;
};

/* complex iq pairs */
struct downsample_state both;
struct downsample_state left;
struct downsample_state right;
/* iq pairs and real mono */
struct demod_state left_demod;
struct demod_state right_demod;
/* real stereo pairs (upsampled) */
struct upsample_stereo stereo;
/*
void usage(void)
{
	fprintf(stderr,
		"rtl_ais, a simple AIS tuner\n"
		"\t and generic dual-frequency FM demodulator\n\n"
		"(probably not a good idea to use with e4000 tuners)\n"
		"Use: rtl_ais [options] [outputfile]\n"
		"\t[-l left_frequency (default: 161.975M)]\n"
		"\t[-r right_frequency (default: 162.025M)]\n"
		"\t    left freq < right freq\n"
		"\t    frequencies must be within 1.2MHz\n"
		"\t[-s sample_rate (default: 24k)]\n"
		"\t    maximum value, might be down to 12k\n"
		"\t[-o output_rate (default: 48k)]\n"
		"\t    must be equal or greater than twice -s value\n"
		"\t[-E toggle edge tuning (default: off)]\n"
		"\t[-D toggle DC filter (default: on)]\n"
		//"\t[-O toggle oversampling (default: off)\n"
		"\t[-d device_index (default: 0)]\n"
		"\t[-g tuner_gain (default: automatic)]\n"
		"\t[-p ppm_error (default: 0)]\n"
		"\t[-R enable RTL chip AGC (default: off)]\n"
		"\t[-A turn off built-in AIS decoder (default: on)]\n"
		"\t    use this option to output samples to file or stdout.\n"
        "\t[-a use auto tuning and auto setting of ppm]\n"
        "\t[-w bandwith in Hz (default the min possible setting)]\n"

		"\tBuilt-in AIS decoder options:\n"
		"\t[-h host (default: 127.0.0.1)]\n"
		"\t[-P port (default: 10110)]\n"
		"\t[-n log NMEA sentences to console (stderr) (default off)]\n"
		"\t[-L log sound levels to console (stderr) (default off)]\n"
		"\t[-S seconds_for_decoder_stats (default 0=off)]\n\n"
        
		"\tWhen the built-in AIS decoder is disabled the samples are sent to\n"
		"\tto [outputfile] (a '-' dumps samples to stdout)\n"
		"\t    omitting the filename also uses stdout\n\n"
		"\tOutput is stereo 2x16 bit signed ints\n\n"
		"\tExmaples:\n"
		"\tReceive AIS traffic,sent UDP NMEA sentences to 127.0.0.1 port 10110\n"
		"\t     and log the senteces to console:\n\n"
		"\trtl_ais -n\n\n"
		"\tTune two fm stations and play one on each channel:\n\n"
		"\trtl_ais -l233.15M  -r233.20M -A  | play -r48k -traw -es -b16 -c2 -V1 - "
		"\n");
	exit(1);
}*/

static void sighandler(int signum)
{
	if (dev != NULL)
		rtlsdr_cancel_async(dev);
	if (signum != 0) {
		aprintf_stderr("Signal caught, exiting! (signal %d)", signum);
		//announce_exceptioncode(com_wordpress_ebc81_rtl_ais_android_core_RtlAisJava_EXIT_SIGNAL_CAUGHT);
	}

	do_exit = 1;
}
int cic_9_tables[][10] = {
	{0,},
	{9, -156,  -97, 2798, -15489, 61019, -15489, 2798,  -97, -156},
	{9, -128, -568, 5593, -24125, 74126, -24125, 5593, -568, -128},
	{9, -129, -639, 6187, -26281, 77511, -26281, 6187, -639, -129},
	{9, -122, -612, 6082, -26353, 77818, -26353, 6082, -612, -122},
	{9, -120, -602, 6015, -26269, 77757, -26269, 6015, -602, -120},
	{9, -120, -582, 5951, -26128, 77542, -26128, 5951, -582, -120},
	{9, -119, -580, 5931, -26094, 77505, -26094, 5931, -580, -119},
	{9, -119, -578, 5921, -26077, 77484, -26077, 5921, -578, -119},
	{9, -119, -577, 5917, -26067, 77473, -26067, 5917, -577, -119},
	{9, -199, -362, 5303, -25505, 77489, -25505, 5303, -362, -199},
};

double log2(double n)
{
	return log(n) / log(2.0);
}


void rotate_90(int16_t *buf, int len)
/* 90 rotation is 1+0j, 0+1j, -1+0j, 0-1j
   or [0, 1, -3, 2, -4, -5, 7, -6] */
{
	int i;
	int16_t tmp;
	for (i=0; i<len; i+=8) {
		tmp = buf[i+2];
		buf[i+2] = -buf[i+3];
		buf[i+3] = tmp;

		buf[i+4] = -buf[i+4];
		buf[i+5] = -buf[i+5];

		tmp = buf[i+6];
		buf[i+6] = buf[i+7];
		buf[i+7] = -tmp;
	}
}

void rotate_m90(int16_t *buf, int len)
/* -90 rotation is 1+0j, 0-1j, -1+0j, 0+1j
   or [0, 1, 3, -2, -4, -5, -7, 6] */
{
	int i;
	int16_t tmp;
	for (i=0; i<len; i+=8) {
		tmp = buf[i+2];
		buf[i+2] = buf[i+3];
		buf[i+3] = -tmp;

		buf[i+4] = -buf[i+4];
		buf[i+5] = -buf[i+5];

		tmp = buf[i+6];
		buf[i+6] = -buf[i+7];
		buf[i+7] = tmp;
	}
}

void fifth_order(int16_t *data, int length, int16_t *hist)
/* for half of interleaved data */
{
	int i;
	int16_t a, b, c, d, e, f;
	a = hist[1];
	b = hist[2];
	c = hist[3];
	d = hist[4];
	e = hist[5];
	f = data[0];
	/* a downsample should improve resolution, so don't fully shift */
	data[0] = (a + (b+e)*5 + (c+d)*10 + f) >> 4;
	for (i=4; i<length; i+=4) {
		a = c;
		b = d;
		c = e;
		d = f;
		e = data[i-2];
		f = data[i];
		data[i/2] = (a + (b+e)*5 + (c+d)*10 + f) >> 4;
	}
	/* archive */
	hist[0] = a;
	hist[1] = b;
	hist[2] = c;
	hist[3] = d;
	hist[4] = e;
	hist[5] = f;
}

void generic_fir(int16_t *data, int length, int *fir, int16_t *hist)
/* Okay, not at all generic.  Assumes length 9, fix that eventually. */
{
	int d, temp, sum;
	for (d=0; d<length; d+=2) {
		temp = data[d];
		sum = 0;
		sum += (hist[0] + hist[8]) * fir[1];
		sum += (hist[1] + hist[7]) * fir[2];
		sum += (hist[2] + hist[6]) * fir[3];
		sum += (hist[3] + hist[5]) * fir[4];
		sum +=            hist[4]  * fir[5];
		data[d] = sum >> 15 ;
		hist[0] = hist[1];
		hist[1] = hist[2];
		hist[2] = hist[3];
		hist[3] = hist[4];
		hist[4] = hist[5];
		hist[5] = hist[6];
		hist[6] = hist[7];
		hist[7] = hist[8];
		hist[8] = temp;
	}
}

void downsample(struct downsample_state *d)
{
	int i, ds_p;
	ds_p = d->downsample_passes;
	for (i=0; i<ds_p; i++) 
	{
		fifth_order(d->buf,   (d->len_in >> i),   d->lp_i_hist[i]);
		fifth_order(d->buf+1, (d->len_in >> i)-1, d->lp_q_hist[i]);
	}
	// droop compensation
	generic_fir(d->buf, d->len_in >> ds_p,cic_9_tables[ds_p], d->droop_i_hist);
	generic_fir(d->buf+1, (d->len_in>> ds_p)-1,cic_9_tables[ds_p], d->droop_q_hist);
}

void multiply(int ar, int aj, int br, int bj, int *cr, int *cj)
{
	*cr = ar*br - aj*bj;
	*cj = aj*br + ar*bj;
}

int polar_discriminant(int ar, int aj, int br, int bj)
{
	int cr, cj;
	double angle;
	multiply(ar, aj, br, -bj, &cr, &cj);
	angle = atan2((double)cj, (double)cr);
	return (int)(angle / 3.14159 * (1<<14));
}

int fast_atan2(int y, int x)
/* pre scaled for int16 */
{
	int yabs, angle;
	int pi4=(1<<12), pi34=3*(1<<12);  // note pi = 1<<14
	if (x==0 && y==0) {
		return 0;
	}
	yabs = y;
	if (yabs < 0) {
		yabs = -yabs;
	}
	if (x >= 0) {
		angle = pi4  - pi4 * (x-yabs) / (x+yabs);
	} else {
		angle = pi34 - pi4 * (x+yabs) / (yabs-x);
	}
	if (y < 0) {
		return -angle;
	}
	return angle;
}

int polar_disc_fast(int ar, int aj, int br, int bj)
{
	int cr, cj;
	multiply(ar, aj, br, -bj, &cr, &cj);
	return fast_atan2(cj, cr);
}

void demodulate(struct demod_state *d)
{
	int i, pcm;
	int16_t *buf = d->buf;
	int16_t *result = d->result;
	pcm = polar_disc_fast(buf[0], buf[1],
		d->pre_r, d->pre_j);
	
	result[0] = (int16_t)pcm;
	for (i = 2; i < (d->buf_len-1); i += 2) {
		// add the other atan types?
		pcm = polar_disc_fast(buf[i], buf[i+1],
			buf[i-2], buf[i-1]);
		result[i/2] = (int16_t)pcm;
	}
	d->pre_r = buf[d->buf_len - 2];
	d->pre_j = buf[d->buf_len - 1];
}

void dc_block_filter(struct demod_state *d)
{
	int i, avg;
	int64_t sum = 0;
	int16_t *result = d->result;
	for (i=0; i < d->result_len; i++) {
		sum += result[i];
	}
	avg = sum / d->result_len;
	avg = (avg + d->dc_avg * 9) / 10;
	for (i=0; i < d->result_len; i++) {
		result[i] -= avg;
	}
	d->dc_avg = avg;
}

void arbitrary_upsample(int16_t *buf1, int16_t *buf2, int len1, int len2)
/* linear interpolation, len1 < len2 */
{
	int i = 1;
	int j = 0;
	int tick = 0;
	double frac;  // use integers...
	while (j < len2) {
		frac = (double)tick / (double)len2;
		buf2[j] = (int16_t)((double)buf1[i-1]*(1-frac) + (double)buf1[i]*frac);
		j++;
		tick += len1;
		if (tick > len2) {
			tick -= len2;
			i++;
		}
		if (i >= len1) {
			i = len1 - 1;
			tick = len2;
		}
	}
}

static void rtlsdr_callback(unsigned char *buf, uint32_t len, void *ctx)
{
	int i;
	if (do_exit) {
		return;}
	pthread_rwlock_wrlock(&both.rw);
	for (i=0; i<len; i++) {
		both.buf[i] = ((int16_t)buf[i]) - 127;
	}
	pthread_rwlock_unlock(&both.rw);
	safe_cond_signal(&ready, &ready_m);
}

void pre_output(void)
{
	int i;
	for (i=0; i<stereo.bl_len; i++) {
		stereo.result[i*2]   = stereo.buf_left[i];
		stereo.result[i*2+1] = stereo.buf_right[i];
	}
}
void output(void)
{
	fwrite(stereo.result, 2, stereo.result_len, file);
}

static void *demod_thread_fn(void *arg)
{
	aprintf_stderr( "\nDemod Thread start...\n");
	while (!do_exit) {
		safe_cond_wait(&ready, &ready_m);
		pthread_rwlock_wrlock(&both.rw);
		downsample(&both);
		memcpy(left.buf,  both.buf, 2*both.len_out);
		memcpy(right.buf, both.buf, 2*both.len_out);
		pthread_rwlock_unlock(&both.rw);
		rotate_90(left.buf, left.len_in);
		downsample(&left);
		memcpy(left_demod.buf, left.buf, 2*left.len_out);
		demodulate(&left_demod);
		if (dc_filter) {
			dc_block_filter(&left_demod);}
		//if (oversample) {
		//	downsample(&left);}
		//fprintf(stderr,"\nUpsample result_len:%d stereo.bl_len:%d :%f\n",left_demod.result_len,stereo.bl_len,(float)stereo.bl_len/(float)left_demod.result_len);
		arbitrary_upsample(left_demod.result, stereo.buf_left, left_demod.result_len, stereo.bl_len);
		rotate_m90(right.buf, right.len_in);
		downsample(&right);
		memcpy(right_demod.buf, right.buf, 2*right.len_out);
		demodulate(&right_demod);
		if (dc_filter) {
			dc_block_filter(&right_demod);}
		//if (oversample) {
		//	downsample(&right);}
		arbitrary_upsample(right_demod.result, stereo.buf_right, right_demod.result_len, stereo.br_len);
		pre_output();
		if(use_internal_aisdecoder){
			// stereo.result -> int_16
			// stereo.result_len -> number of samples for each channel
			run_rtlais_decoder(stereo.result,stereo.result_len);
		}
		else{
			output();
		}
	}
	aprintf_stderr( "\nDemod Thread end...\n");
	rtlsdr_cancel_async(dev);
	thread_detach();
	//free_ais_decoder();
	return 0;
}

// static void *ais_decoder_thread_fn(void *threadarg)
// {
//     struct init_ais_decoder_data *my_data;
//     my_data = (struct init_ais_decoder_data *) threadarg;
//         char * host = my_data->host;
//         char * port = my_data->port;
//         int show_levels = my_data->show_levels;
//         int debug_nmea = my_data->debug_nmea;
//         int buf_len = my_data->buf_len;
//         int time_print_stats = my_data->time_print_stats;
//     int ret=init_ais_decoder(host,port,show_levels,debug_nmea,stereo.bl_len,seconds_for_decoder_stats);
//     if(ret != 0){
//             fprintf(stderr,"Error initializing built-in AIS decoder\n");
//             rtlsdr_cancel_async(dev);
//             rtlsdr_close(dev);
//             //exit(1);
//     }
//     pthread_exit(NULL);
// }

void downsample_init(struct downsample_state *dss)
/* simple ints should be already set */
{
	int i, j;
	dss->buf = malloc(dss->len_in * sizeof(int16_t));
	dss->rate_out = dss->rate_in / dss->downsample;
	
	//dss->downsample_passes = (int)log2(dss->downsample);
	dss->len_out = dss->len_in / dss->downsample;
	for (i=0; i<10; i++) { for (j=0; j<6; j++) {
		dss->lp_i_hist[i][j] = 0;
		dss->lp_q_hist[i][j] = 0;
	}}
	pthread_rwlock_init(&dss->rw, NULL);
}

void demod_init(struct demod_state *ds)
{
	ds->buf = malloc(ds->buf_len * sizeof(int16_t));
	ds->result = malloc(ds->result_len * sizeof(int16_t));
}

void stereo_init(struct upsample_stereo *us)
{
	us->buf_left  = malloc(us->bl_len * sizeof(int16_t));
	us->buf_right = malloc(us->br_len * sizeof(int16_t));
	us->result    = malloc(us->result_len * sizeof(int16_t));
}

struct command{
	unsigned char cmd;
	unsigned int param;
}__attribute__((packed));

static int set_gain_by_index(rtlsdr_dev_t *_dev, unsigned int index)
{
	int res = 0;
	int* gains;
	int count = rtlsdr_get_tuner_gains(_dev, NULL);

	if (count > 0 && (unsigned int)count > index) {
		gains = malloc(sizeof(int) * count);
		count = rtlsdr_get_tuner_gains(_dev, gains);

		res = rtlsdr_set_tuner_gain(_dev, gains[index]);

		free(gains);
	}

	return res;
}

static int set_gain_by_perc(rtlsdr_dev_t *_dev, unsigned int percent)
{
	int res = 0;
	int* gains;
	int count = rtlsdr_get_tuner_gains(_dev, NULL);
	unsigned int index = (percent * count) / 100;
	if (index < 0) index = 0;
	if (index >= (unsigned int) count) index = count - 1;

	gains = malloc(sizeof(int) * count);
	count = rtlsdr_get_tuner_gains(_dev, gains);

	res = rtlsdr_set_tuner_gain(_dev, gains[index]);

	free(gains);

	return res;
}


static void command_worker(void *arg)
{
	int left, received = 0;
	fd_set readfds;
	struct command cmd={0, 0};
	struct timeval tv= {1, 0};
	int r = 0;
	uint32_t tmp;
	int nearest_bandwith = 0;

	while(1) {
		left=sizeof(cmd);
		while(left >0) {
			FD_ZERO(&readfds);
			FD_SET(s, &readfds);
			tv.tv_sec = 1;
			tv.tv_usec = 0;
			r = select(s+1, &readfds, NULL, NULL, &tv);
			if(r) {
				received = recv(s, (char*)&cmd+(sizeof(cmd)-left), left, 0);
				left -= received;
			}
			if(received == SOCKET_ERROR || do_exit) {
				aprintf("comm recv bye");
				sighandler(0);
				thread_detach();
				pthread_exit(NULL);
			}
		}
		switch(cmd.cmd) {
		case 0x01:
			aprintf("set freq %d", ntohl(cmd.param));
			rtlsdr_set_center_freq(dev,ntohl(cmd.param));
			break;
		case 0x02:
			aprintf("set sample rate %d", ntohl(cmd.param));
			rtlsdr_set_sample_rate(dev, ntohl(cmd.param));
			break;
		case 0x03:
			aprintf("set gain mode %d", ntohl(cmd.param));
			rtlsdr_set_tuner_gain_mode(dev, ntohl(cmd.param));
			break;
		case 0x04:
			aprintf("set gain %d", ntohl(cmd.param));
			rtlsdr_set_tuner_gain(dev, ntohl(cmd.param));
			break;
		case 0x05:
			aprintf("set freq correction %d", ntohl(cmd.param));
			rtlsdr_set_freq_correction(dev, ntohl(cmd.param));
			break;
		case 0x06:
			tmp = ntohl(cmd.param);
			aprintf("set if stage %d gain %d", tmp >> 16, (short)(tmp & 0xffff));
			rtlsdr_set_tuner_if_gain(dev, tmp >> 16, (short)(tmp & 0xffff));
			break;
		case 0x07:
			aprintf("set test mode %d", ntohl(cmd.param));
			rtlsdr_set_testmode(dev, ntohl(cmd.param));
			break;
		case 0x08:
			aprintf("set agc mode %d", ntohl(cmd.param));
			rtlsdr_set_agc_mode(dev, ntohl(cmd.param));
			break;
		case 0x09:
			aprintf("set direct sampling %d", ntohl(cmd.param));
			rtlsdr_set_direct_sampling(dev, ntohl(cmd.param));
			break;
		case 0x0a:
			aprintf("set offset tuning %d", ntohl(cmd.param));
			rtlsdr_set_offset_tuning(dev, ntohl(cmd.param));
			break;
		case 0x0b:
			aprintf("set rtl xtal %d", ntohl(cmd.param));
			rtlsdr_set_xtal_freq(dev, ntohl(cmd.param), 0);
			break;
		case 0x0c:
			aprintf("set tuner xtal %d", ntohl(cmd.param));
			rtlsdr_set_xtal_freq(dev, 0, ntohl(cmd.param));
			break;
		case 0x0d:
			aprintf("set tuner gain by index %d", ntohl(cmd.param));
			set_gain_by_index(dev, ntohl(cmd.param));
			break;
		case 0x0e://ebc
			aprintf("set bandwidth  %d", ntohl(cmd.param));
			nearest_bandwith = nearest_bandwidth(dev,cmd.param);
			rtlsdr_set_tuner_bandwidth(dev, nearest_bandwith);
			break;
		case 0x7e:
			aprintf("client requested to close rtl_ais_android");
			sighandler(0);
			break;
		case 0x7f:
			set_gain_by_perc(dev, ntohl(cmd.param));
			break;
		default:
			break;
		}
		cmd.cmd = 0xff;
	}
	thread_detach();
	pthread_exit(NULL);
	return;
}


void rtl_ais_close() {
	sighandler(0);
}
int rtl_ais_isrunning() 
{
	int isit_running = 0;
	pthread_mutex_lock(&running_mutex);
	isit_running = is_running;
	pthread_mutex_unlock(&running_mutex);
	return isit_running;
}

// ebc
void set_isrunning( int status )
{
	pthread_mutex_lock(&running_mutex);
	is_running = status;
	pthread_mutex_unlock(&running_mutex);
}

void rtl_ais_main(int usbfd, const char * uspfs_path_input, int argc, char **argv)
{
// ebc: not needed not a standalone app 
//#ifndef WIN32
//	struct sigaction sigact;
//#endif	
	char *filename = NULL;
	int r, opt;
	int i, gain = AUTO_GAIN; /* tenths of a dB */
	int dev_index = 0;
	int dev_given = 0;
	int rtl_agc=0;
	int custom_ppm = 0;
    int custom_bandwidth = 0;
    int use_autotune = 0;
    int bandwidth = 0;
	int left_freq = 161975000;
	int right_freq = 162025000;
	int sample_rate = 24000;
	int output_rate = 48000;
	int dongle_freq, dongle_rate, delta;
	int edge = 0;
/* Aisdecoder */
	int	show_levels=0;
	int debug_nmea = 0;
	char ais_port[50];
	char ais_host[50];
	char *pos;
	int count;
    int gains[100];
    int bandwidths[100];
    //int check_ppm = 0;
    //int ppm_auto_time = 10;
	int ppm_error = 0;
	struct sockaddr_in local, remote;
	SOCKET listensocket;
	socklen_t rlen;
	fd_set readfds;
	struct timeval tv = {1,0};
	struct linger ling = {1,0};
	pthread_attr_t attr;
	void *status;
	char* addr_listen = "127.0.0.1";
	int port_listen = 1234;
	int without_tcp_worker = 0;
	//ebc
	//------------------------------------------------------
	ais_port[0] = '\000';
	ais_host[0] = '\000';
	set_isrunning(1);
	if (usbfd != -1 && (!(fcntl(usbfd, F_GETFL) != -1 || errno != EBADF))) {
		aprintf_stderr("Invalid file descriptor %d, - %s", usbfd, strerror(errno));
		announce_exceptioncode(com_wordpress_ebc81_rtl_ais_android_core_RtlAisJava_EXIT_INVALID_FD);
		set_isrunning(0);
		return;
	}
	//------------------------------------------------------

	pthread_cond_init(&ready, NULL);
	pthread_mutex_init(&ready_m, NULL);

	while ((opt = getopt(argc, argv, "l:r:s:o:EOD:d:g:p:RAP:w:h:nLS:ax?")) != -1)
	{
		switch (opt) {
		case 'l':
			left_freq = (int)atofs(optarg);
			break;
		case 'r':
			right_freq = (int)atofs(optarg);
			break;
		case 's':
			sample_rate = (int)atofs(optarg);
			break;
		case 'o':
			output_rate = (int)atofs(optarg);
			break;
		case 'E':
			edge = !edge;
			break;
		case 'O':
			oversample = !oversample;
			break;
        case 'D':
            dc_filter = !dc_filter;
            break;
		case 'd':
			dev_index = verbose_device_search(optarg);
			dev_given = 1;
			break;
		case 'g':
			gain = (int)(atof(optarg) * 10);
			break;
		case 'p':
			ppm_error = atoi(optarg);
			custom_ppm = 1;
			aprintf_stderr("ppm_error %d \n",ppm_error);
			break;
		case 'R':
			rtl_agc=1;
			break;
		case 'A':
			use_internal_aisdecoder=0;
			break;
		case 'P':
			strncpy(ais_port,optarg,sizeof(ais_port));			
			
			if ((pos=strchr(ais_port, '\n')) != NULL)
				*pos = '\0';
			//ais_port=strdup(optarg);
			break;
        case 'w':
            bandwidth = atoi(optarg);
            custom_bandwidth = 1;
            break;
		case 'h':
			strncpy(ais_host,optarg,sizeof(ais_host));
			if ((pos=strchr(ais_host, '\n')) != NULL)
				*pos = '\0';
			//ais_host=strdup(optarg);
			break;
        case 'n':
            debug_nmea = 1;
			aprintf_stderr("debug_nmea %d \n",debug_nmea);
            break;
		case 'L':
			show_levels=1;
			break;
		case 'S':
			seconds_for_decoder_stats=atoi(optarg);
			break;
        case 'a':
            use_autotune = 1;
            break;
		case 'x':
			without_tcp_worker = 1;
			break;

		case '?':
		default:
			aprintf_stderr("Unexpected argument '%c' with value '%s' received as an argument", opt, optarg);
			announce_exceptioncode(com_wordpress_ebc81_rtl_ais_android_core_RtlAisJava_EXIT_WRONG_ARGS);
			set_isrunning(0);
			return;
		}
	}

	if (argc <= optind) {
		filename = "-";
	} else {
		filename = argv[optind];
	}

	if (left_freq > right_freq) {
		aprintf_stderr("left_freq > right_freq");
		//usage();
		announce_exceptioncode(com_wordpress_ebc81_rtl_ais_android_core_RtlAisJava_EXIT_WRONG_ARGS);
		set_isrunning(0);
		return;// 2;
	}
	
	if(ais_host==NULL || ais_host[0] == '\000'){
		sprintf(ais_host,"127.0.0.1");
		//ais_host=strdup("127.0.0.1");
	}
	if(ais_port==NULL || ais_port[0] == '\000'){
		sprintf(ais_port,"127.0.0.1");
		//ais_port=strdup("10110");
	}
	
	/* precompute rates */
	dongle_freq = left_freq/2 + right_freq/2;
	if (edge) {
		dongle_freq -= sample_rate/2;}
	delta = right_freq - left_freq;
	if (delta > 1.2e6) {
		aprintf_stderr("Frequencies may be at most 1.2MHz apart.");
		announce_exceptioncode(com_wordpress_ebc81_rtl_ais_android_core_RtlAisJava_EXIT_WRONG_ARGS);
		set_isrunning(0);
		return;
		//exit(1);
	}
	if (delta < 0) {
		aprintf_stderr("Left channel must be lower than right channel.");
		announce_exceptioncode(com_wordpress_ebc81_rtl_ais_android_core_RtlAisJava_EXIT_WRONG_ARGS);
		set_isrunning(0);
		return;
		//exit(1);
	}
	i = (int)log2(2.4e6 / delta);
	dongle_rate = delta * (1<<i);
	both.rate_in = dongle_rate;
	both.rate_out = delta * 2;
	i = (int)log2(both.rate_in/both.rate_out);
	both.downsample_passes = i;
	both.downsample = 1 << i;
	left.rate_in = both.rate_out;
	i = (int)log2(left.rate_in / sample_rate);
	left.downsample_passes = i;
	left.downsample = 1 << i;
	left.rate_out = left.rate_in / left.downsample;
	
	right.rate_in = left.rate_in;
	right.rate_out = left.rate_out;
	right.downsample = left.downsample;
	right.downsample_passes = left.downsample_passes;

	if (left.rate_out > output_rate) {
		aprintf_stderr( "Channel bandwidth too high or output bandwidth too low.");
		announce_exceptioncode(com_wordpress_ebc81_rtl_ais_android_core_RtlAisJava_EXIT_WRONG_ARGS);
		set_isrunning(0);
		exit(1);
	}

	stereo.rate = output_rate;

	if (edge) {
		aprintf_stderr( "Edge tuning enabled.\n");
	} else {
		aprintf_stderr( "Edge tuning disabled.\n");
	}
	if (dc_filter) {
		aprintf_stderr( "DC filter enabled.\n");
	} else {
		aprintf_stderr( "DC filter disabled.\n");
	}
	if (rtl_agc) {
		aprintf_stderr( "RTL AGC enabled.\n");
	} else {
		aprintf_stderr( "RTL AGC disabled.\n");
	}
	if (use_internal_aisdecoder) {
		aprintf_stderr( "Internal AIS decoder enabled.\n");
	} else {
		aprintf_stderr( "Internal AIS decoder disabled.\n");
	}
	if (use_autotune) 
	{
        if (use_internal_aisdecoder) 
            fprintf(stderr, "Use auto tuning for rtl receiver.\n");
        else 
            fprintf(stderr, "Auto tuning can only be used with internal AIS. sorry disabled! \n");
	}
	
	aprintf_stderr( "Buffer size: %0.2f mS\n", 1000 * (double)DEFAULT_BUF_LENGTH / (double)dongle_rate);
	aprintf_stderr( "Downsample factor: %i\n", both.downsample * left.downsample);
	aprintf_stderr( "Low pass: %i Hz\n", left.rate_out);
	aprintf_stderr( "Output: %i Hz\n", output_rate);

	/* precompute lengths */
	both.len_in  = DEFAULT_BUF_LENGTH;
	both.len_out = both.len_in / both.downsample;
	left.len_in  = both.len_out;
	right.len_in = both.len_out;
	left.len_out = left.len_in / left.downsample;
	right.len_out = right.len_in / right.downsample;
	left_demod.buf_len = left.len_out;
	left_demod.result_len = left_demod.buf_len / 2;
	right_demod.buf_len = left_demod.buf_len;
	right_demod.result_len = left_demod.result_len;
//	stereo.bl_len = (int)((long)(DEFAULT_BUF_LENGTH/2) * (long)output_rate / (long)dongle_rate); -> Doesn't work on Linux
	stereo.bl_len = (int)((double)(DEFAULT_BUF_LENGTH/2) * (double)output_rate / (double)dongle_rate);
	stereo.br_len = stereo.bl_len;
	stereo.result_len = stereo.br_len * 2;
	stereo.rate = output_rate;

	r = 0;
	
	if (usbfd == -1) // ebc
	{
		if (!dev_given) 
		{
			dev_index = verbose_device_search("0");
		}
		if (dev_index < 0) {
			aprintf_stderr("No supported devices found.");
			announce_exceptioncode( com_wordpress_ebc81_rtl_ais_android_core_RtlAisJava_EXIT_NO_DEVICES );
			set_isrunning(0);
			return;
		}
		aprintf("Opening device with rtlsdr_open");
		r = rtlsdr_open(&dev, (uint32_t)dev_index);
	} else { // ebc
		aprintf("Opening device with rtlsdr_open2 fd %d at %s", usbfd, uspfs_path_input);
		r = rtlsdr_open2(&dev,(uint32_t)dev_index, usbfd, uspfs_path_input);
	}
	if (NULL == dev) 
	{
		aprintf_stderr("Failed to open rtlsdr device #%d.", dev_index);
		announce_exceptioncode( com_wordpress_ebc81_rtl_ais_android_core_RtlAisJava_EXIT_FAILED_TO_OPEN_DEVICE );
		set_isrunning(0);
		return;
	}


	if (r < 0) {
		announce_exceptioncode( r );
		set_isrunning(0);
		aprintf("Failed to open rtlsdr device #%d.\n", dev_index);
		return;
	}

	downsample_init(&both);
	downsample_init(&left);
	downsample_init(&right);
	demod_init(&left_demod);
	demod_init(&right_demod);
	stereo_init(&stereo);
/*
* ebc: not needed not a standalone app
#ifndef WIN32	
	sigact.sa_handler = sighandler;
	sigemptyset(&sigact.sa_mask);
	sigact.sa_flags = 0;
	sigaction(SIGINT, &sigact, NULL);
	sigaction(SIGTERM, &sigact, NULL);
	sigaction(SIGQUIT, &sigact, NULL);
	sigaction(SIGPIPE, &sigact, NULL);
#else
	signal(SIGINT, sighandler);
	signal(SIGTERM, sighandler);

#endif*/

	if(!use_internal_aisdecoder){
		if (strcmp(filename, "-") == 0) { /* Write samples to stdout */
			file = stdout;
	#ifdef WIN32		
			setmode(fileno(stdout), O_BINARY); // Binary mode, avoid text mode
	#endif		
			setvbuf(stdout, NULL, _IONBF, 0);
		} else {
			file = fopen(filename, "wb");
			if (!file) {
				aprintf_stderr( "Failed to open %s\n", filename);
				announce_exceptioncode(  com_wordpress_ebc81_rtl_ais_android_core_RtlAisJava_EXIT_FAILED_TO_OPEN_DEVICE );
				set_isrunning(0);
				return;
			}
		}
	}
	else{ // Internal AIS decoder
		
		int ret=init_ais_decoder(ais_host,ais_port,show_levels,debug_nmea,stereo.bl_len,seconds_for_decoder_stats);
		aprintf_stderr("ais_host=%s ais_port=%s,show_levels=%d,debug_nmea=%d",ais_host,ais_port, show_levels,debug_nmea);
		if(ret != 0){
			aprintf_stderr("Error initializing built-in AIS decoder\n");
			rtlsdr_cancel_async(dev);
			rtlsdr_close(dev);
			announce_exceptioncode(  com_wordpress_ebc81_rtl_ais_android_core_RtlAisJava_EXIT_AIS_DECODERR );
			set_isrunning(0);
			return;
		}
	}
	
	rtlsdr_set_tuner_gain_mode(dev, GAIN_MODE_SENSITIVITY);

    count = rtlsdr_get_tuner_gains(dev, NULL);
    aprintf_stderr( "Supported gain values (%d): ", count);

    count = rtlsdr_get_tuner_gains(dev, gains);
    for (i = 0; i < count; i++)
        aprintf_stderr( "%.1f ", gains[i] / 10.0);
    aprintf_stderr( "\n");
	
	
	
	/* Set the tuner gain */
	if (gain == AUTO_GAIN) {
		verbose_auto_gain(dev);
	} else {
		gain = nearest_gain(dev, gain);
		verbose_gain_set(dev, gain);
	}
	if(rtl_agc){
		int r = rtlsdr_set_agc_mode(dev, 1);
		if(r<0)	{
			aprintf_stderr("Error seting RTL AGC mode ON\n");
			//return;
		}
		else {
			aprintf_stderr("RTL AGC mode ON\n");
		}
	}
	if (!custom_ppm) {
		verbose_ppm_eeprom(dev, &ppm_error);
	}
	
	count = rtlsdr_get_tuner_bandwidths(dev, NULL);
    aprintf_stderr( "Supported bandwidth values (%d): ", count);

    count = rtlsdr_get_tuner_bandwidths(dev, bandwidths);
    for (i = 0; i < count; i++)
        aprintf_stderr( "%d ", bandwidths[i]);
    aprintf_stderr( "\n");	
	
	
	//ebc TO DO bandwidth is never used, should used here
	verbose_set_bandwidth(dev, 300000);
    
	
	verbose_ppm_set(dev, ppm_error);
	aprintf_stderr("ppm_error %d \n",ppm_error);
	
	/* Set the tuner frequency */
	verbose_set_frequency(dev, dongle_freq);

	/* Set the sample rate */
	verbose_set_sample_rate(dev, dongle_rate);

	/* Reset endpoint before we start reading from it (mandatory) */
	r = verbose_reset_buffer(dev);
	if (r < 0) {
		aprintf_stderr("WARNING: Failed to reset buffers.");
		announce_exceptioncode( com_wordpress_ebc81_rtl_ais_android_core_RtlAisJava_EXIT_FAILED_TO_OPEN_DEVICE );
		set_isrunning(0);
		return;
	}
	
	memset(&local,0,sizeof(local));
	if ( without_tcp_worker )
	{
		announce_success();
		aprintf("standalone app: not listining on TCP comand stream-> argument -x");
	}
	if ( !without_tcp_worker)
	{
		local.sin_family = AF_INET;
		local.sin_port = htons(port_listen);
		local.sin_addr.s_addr = inet_addr(addr_listen);

		listensocket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
		r = 1;
		setsockopt(listensocket, SOL_SOCKET, SO_REUSEADDR, (char *)&r, sizeof(int));
		setsockopt(listensocket, SOL_SOCKET, SO_LINGER, (char *)&ling, sizeof(ling));
		bind(listensocket,(struct sockaddr *)&local,sizeof(local));

		r = fcntl(listensocket, F_GETFL, 0);
		r = fcntl(listensocket, F_SETFL, r | O_NONBLOCK);

		announce_success();
		aprintf("listening on %s:%d...", addr_listen, port_listen);
		listen(listensocket,1);
	
		while(1) {
			FD_ZERO(&readfds);
			FD_SET(listensocket, &readfds);
			tv.tv_sec = 1;
			tv.tv_usec = 0;
			r = select(listensocket+1, &readfds, NULL, NULL, &tv);
			if(do_exit) {
				goto out;
			} else if(r) {
				rlen = sizeof(remote);
				s = accept(listensocket,(struct sockaddr *)&remote, &rlen);
				break;
			}
		}
	
		setsockopt(s, SOL_SOCKET, SO_LINGER, (char *)&ling, sizeof(ling));
		aprintf("client accepted!");

		pthread_attr_init(&attr);
		pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_JOINABLE);
		r = pthread_create(&command_thread, &attr, (void *) command_worker, NULL);
		pthread_attr_destroy(&attr);
	}

	pthread_create(&demod_thread, NULL, demod_thread_fn, (void *)(NULL));
	//* Read samples from the device asynchronously. This function will block until
	//* it is being canceled using rtlsdr_cancel_async()
	rtlsdr_read_async(dev, rtlsdr_callback, (void *)(NULL),
			      DEFAULT_ASYNC_BUF_NUMBER,
			      DEFAULT_BUF_LENGTH);

	pthread_join(command_thread, &status);
	closesocket(s);

	if (do_exit) {
		aprintf_stderr( "\nUser cancel, exiting...\n");}
	else {
		aprintf_stderr( "\nLibrary error %d, exiting...\n", r);
	}
	if ( dev != NULL )
		rtlsdr_cancel_async(dev);

	safe_cond_signal(&ready, &ready_m);
	pthread_cond_destroy(&ready);
	pthread_mutex_destroy(&ready_m);

	if (file != stdout) {
		if(file)
			fclose(file);
	}
	out:
	rtlsdr_close(dev);
	free_ais_decoder();
	if ( !without_tcp_worker)
	{
		closesocket(listensocket);
		closesocket(s);
	}

	announce_exceptioncode(com_wordpress_ebc81_rtl_ais_android_core_RtlAisJava_EXIT_OK);
	set_isrunning(0);
	aprintf("rtl_ais_andro says bye! ");
	return;// r >= 0 ? r : -r;
}

// vim: tabstop=8:softtabstop=8:shiftwidth=8:noexpandtab
