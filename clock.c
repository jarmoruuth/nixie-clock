// Nixie clock.
//
// Copyright (c) 2020 Alexander Krotov.
//
// Code is partially derived from following sources.

/*
 * newtest.c
 *
 * Copyright (c) 2014 Jeremy Garff <jer @ jers.net>
 *
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without modification, are permitted
 * provided that the following conditions are met:
 *
 *     1.  Redistributions of source code must retain the above copyright notice, this list of
 *         conditions and the following disclaimer.
 *     2.  Redistributions in binary form must reproduce the above copyright notice, this list
 *         of conditions and the following disclaimer in the documentation and/or other materials
 *         provided with the distribution.
 *     3.  Neither the name of the owner nor the names of its contributors may be used to endorse
 *         or promote products derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND
 * FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA,
 * OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT
 * OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 */

/**
 * example C code using libcurl and json-c
 * to post and return a payload using
 * http://jsonplaceholder.typicode.com
 *
 * License:
 *
 * This code is licensed under MIT license
 * https://opensource.org/licenses/MIT
 *
 * Requirements:
 *
 * json-c - https://github.com/json-c/json-c
 * libcurl - http://curl.haxx.se/libcurl/c
 *
 * Build:
 *
 * cc curltest.c -lcurl -ljson-c -o curltest
 *
 * Run:
 *
 * ./curltest
 * 
 */


#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/time.h>
#include <signal.h>
#include <stdarg.h>
#include <getopt.h>
#include <time.h>

#include "clk.h"
#include "gpio.h"
#include "dma.h"
#include "pwm.h"
#include "version.h"

#include <wiringPi.h>

/* json-c (https://github.com/json-c/json-c) */
#include <json-c/json.h>

/* libcurl (http://curl.haxx.se/libcurl/c) */
#include <curl/curl.h>
#include "ws2811.h"

// defaults for cmdline options
#define TARGET_FREQ             WS2811_TARGET_FREQ
#define GPIO_PIN                18
#define DMA                     10
#define STRIP_TYPE              WS2811_STRIP_GBR		// WS2812/SK6812RGB integrated chip+leds

#define LED_COUNT               6

/* Thermometer readings for inside and outside temperature. */
struct thermometers {
	int inside;
	int outside;
};

/* holder for curl fetch */
struct curl_fetch_st {
    char *payload;
    size_t size;
};

int clear_on_exit = 0;

static uint8_t running = 1;

// Handle interrupt signal.
static void ctrl_c_handler(int signum)
{
	(void)(signum);
        running = 0;
}

static void setup_handlers(void)
{
    struct sigaction sa =
    {
        .sa_handler = ctrl_c_handler,
    };

    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);
}

/* callback for curl fetch */
size_t curl_callback (void *contents, size_t size, size_t nmemb, void *userp) {
    size_t realsize = size * nmemb;                             /* calculate buffer size */
    struct curl_fetch_st *p = (struct curl_fetch_st *) userp;   /* cast pointer to fetch struct */

    /* expand buffer using a temporary pointer to avoid memory leaks */
    char * temp = realloc(p->payload, p->size + realsize + 1);

    /* check allocation */
    if (temp == NULL) {
      /* this isn't good */
      fprintf(stderr, "ERROR: Failed to expand buffer in curl_callback");
      /* free buffer */
      free(p->payload);
      /* return */
      return 1;
    }

    /* assign payload */
    p->payload = temp;

    /* copy contents to buffer */
    memcpy(&(p->payload[p->size]), contents, realsize);

    /* set new buffer size */
    p->size += realsize;

    /* ensure null termination */
    p->payload[p->size] = 0;

    /* return size */
    return realsize;
}

/* fetch and return url body via curl */
CURLcode curl_fetch_url(CURL *ch, const char *url, struct curl_fetch_st *fetch) {
    CURLcode rcode;                   /* curl result code */

    /* init payload */
    fetch->payload = (char *) calloc(1, sizeof(fetch->payload));

    /* check payload */
    if (fetch->payload == NULL) {
        /* log error */
        fprintf(stderr, "ERROR: Failed to allocate payload in curl_fetch_url");
        /* return error */
        return CURLE_FAILED_INIT;
    }

    /* init size */
    fetch->size = 0;

    /* set url to fetch */
    curl_easy_setopt(ch, CURLOPT_URL, url);

    /* set calback function */
    curl_easy_setopt(ch, CURLOPT_WRITEFUNCTION, curl_callback);

    /* pass fetch struct pointer */
    curl_easy_setopt(ch, CURLOPT_WRITEDATA, (void *) fetch);

    /* set default user agent */
    curl_easy_setopt(ch, CURLOPT_USERAGENT, "libcurl-agent/1.0");

    /* set timeout */
    curl_easy_setopt(ch, CURLOPT_TIMEOUT, 15);

    /* enable location redirects */
    curl_easy_setopt(ch, CURLOPT_FOLLOWLOCATION, 1);

    /* set maximum allowed redirects */
    curl_easy_setopt(ch, CURLOPT_MAXREDIRS, 1);

    /* fetch the url */
    rcode = curl_easy_perform(ch);

    /* return */
    return rcode;
}

// Turn off all the leds
static void matrix_clear(ws2811_t *ledstring)
{
    	int i;
    	for (i = 0; i < LED_COUNT; i++) {
       	    ledstring->channel[0].leds[i] = 0;
    	}
}

// Set leads to random colors.
void matrix_bottom(ws2811_t *ledstring)
{
    	int i;

	// Different kinds of read and orange.
	ws2811_led_t dotcolors[] =
	{
    	    0x00000004,
            0x00000208,
            0x00000004,
            0x00000008,
            0x00000004,
            0x00000004,
            0x00000208,
            0x00000004,
            0,
        };

        // Set all leds to random colors.
        for (i = 0; i < LED_COUNT; i++) {
            ledstring->channel[0].leds[i] = dotcolors[rand()%(sizeof(dotcolors)/sizeof(dotcolors[0]))];
        }
}

// Show given digit on the idicator.
static void show_digit(int d)
{
	static int translate[] = { 3, 4, 5, 13, 12, 8, 9, 1, 0, 2 /*14*/ };
        d = translate[d];
	digitalWrite(21, d&1 ? HIGH: LOW);
	digitalWrite(7, d&2 ? HIGH: LOW);
	digitalWrite(9, d&4 ? HIGH: LOW);
	digitalWrite(8, d&8 ? HIGH: LOW);
}

// Set dots on the indicator.
static void show_dots(int d)
{
	digitalWrite(11, d&1 ? HIGH: LOW);
	digitalWrite(22, d&2 ? HIGH: LOW);
}

// Set digit position
static void set_digit(int d)
{
	digitalWrite(14, d&1 ? HIGH: LOW);
	digitalWrite(12, d&2 ? HIGH: LOW);
	digitalWrite(13, d&4 ? HIGH: LOW);
}

// Show digit on the given indicator position.
static void display_pos(int pos, int d)
{
	set_digit(pos);
        show_digit(d);

	// Turn on indicator power for 3ms to display the digit
	digitalWrite(10, HIGH);
        usleep(3000);
	digitalWrite(10, LOW);
	// Wait to let the power supply reset.
        usleep(100);
}

// Run all the indicator digits.
static void run_all_digits()
{
	int i, j;
        digitalWrite(10, HIGH);
	for (i=0; i<10; i++) {
            show_digit(i);
	    for (j=1; j<=6; j++) {
	        set_digit(j);
                usleep(3000);
	    }
	}
	digitalWrite(10, LOW);
}

// Display current time
static void display_time(struct tm *tm)
{
        display_pos(1, tm->tm_hour/10);
        display_pos(2, tm->tm_hour%10);
        display_pos(3, tm->tm_min/10);
        display_pos(4, tm->tm_min%10);
        display_pos(5, tm->tm_sec/10);
        display_pos(6, tm->tm_sec%10);
}

// Light the bars
static void display_bars(struct timeval *tv)
{
	if (tv->tv_usec < 500000) {
	    set_digit(0);
       	    digitalWrite(10, HIGH);
            usleep(3000);
	} else {
	    set_digit(7);
            digitalWrite(10, HIGH);
            usleep(3000);
	}
       	digitalWrite(10, LOW);
}

// Display the running dots
static void display_dots(struct timeval *tv)
{
	int light = 0;
	if (tv->tv_sec % 7 == 0) {
	    // Run the dosts right to left
	    // First find the dot number.
	    int d = tv->tv_usec/80000;
	    // We have total of 12 dots.
	    if (d<12) {
		// Seth the indicator number.
	        set_digit(6-d/2);
	        if (d%2==0) {
		    // Set the right dot
                    show_dots(0);
	        } else {
		    // Set the left dot
	            show_dots(3);
	        }
		light = 1;
	    }
	} else if (tv->tv_sec%7 == 1) {
	    int d = tv->tv_usec/80000;
	    if (d<12) {
	        set_digit(d/2+1);
	        if (d%2==0) {
	            show_dots(3);
	        } else {
                    show_dots(0);
	        }
		light = 1;
	    }
	}

	if (light) {
	    digitalWrite(7, HIGH);
	    digitalWrite(9, HIGH);
	    // Ligh the dot.
            digitalWrite(10, HIGH);
            usleep(3000);
            digitalWrite(10, LOW);
	    // Reset the dot.
	    show_dots(1);
	}
}

// Display the leds.
static void display_leds(ws2811_t *ledstring)
{
    	ws2811_return_t ret;
        matrix_bottom(ledstring);

        if ((ret = ws2811_render(ledstring)) != WS2811_SUCCESS)
        {
            fprintf(stderr, "ws2811_render failed: %s\n", ws2811_get_return_t_str(ret));
        }
}

// Read ds18b20 sensor and update inside temperature
static void update_inside_temperature(struct thermometers *temp)
{
	int inside_mc;
	FILE *ds18b20 = fopen("/sys/bus/w1/devices/28-01192d308339/temperature", "r");
	if (ds18b20 != NULL) {
	    // Read the value in micro-celcius.
	    fscanf(ds18b20, "%d", &inside_mc);
	    fclose(ds18b20);
	    temp->inside = inside_mc/1000;
	}
}

// Read the outside temperature from Openweathermap
static void update_outside_temperature(struct thermometers *temp)
{
    	CURL *ch;                                               /* curl handle */
    	CURLcode rcode;                                         /* curl result code */

    	json_object *json;                                      /* json post body */
    	enum json_tokener_error jerr = json_tokener_success;    /* json parse error */

    	struct curl_fetch_st curl_fetch;                        /* curl fetch struct */
    	struct curl_fetch_st *cf = &curl_fetch;                 /* pointer to fetch struct */
    	struct curl_slist *headers = NULL;                      /* http headers to send with request */

    	/* url to the weather map */
        char *url = "http://api.openweathermap.org/data/2.5/weather?q=Helsinki,fi&APPID=fa1e15c8e21e5c84e74ef29c46047632";

    	/* init curl handle */
    	if ((ch = curl_easy_init()) == NULL) {
            /* log error */
            fprintf(stderr, "ERROR: Failed to create curl handle in fetch_session");
            /* return error */
        }

        /* set content type */
        headers = curl_slist_append(headers, "Accept: application/json");
        headers = curl_slist_append(headers, "Content-Type: application/json");

        /* set curl options */
        curl_easy_setopt(ch, CURLOPT_CUSTOMREQUEST, "GET");
        curl_easy_setopt(ch, CURLOPT_HTTPHEADER, headers);

        /* fetch page and capture return code */
        rcode = curl_fetch_url(ch, url, cf);

        /* cleanup curl handle */
        curl_easy_cleanup(ch);

        /* free headers */
        curl_slist_free_all(headers);

        /* check return code */
        if (rcode != CURLE_OK || cf->size < 1) {
            /* log error */
            fprintf(stderr, "ERROR: Failed to fetch url (%s) - curl said: %s",
                    url, curl_easy_strerror(rcode));
            /* return error */
            return;
        }

        /* check payload */
        if (cf->payload != NULL) {
            /* parse return */
            json = json_tokener_parse_verbose(cf->payload, &jerr);
            /* free payload */
            free(cf->payload);
        } else {
            /* error */
            fprintf(stderr, "ERROR: Failed to populate payload");
            /* free payload */
            free(cf->payload);
            /* return */
            return;
        }

        /* check error */
        if (jerr != json_tokener_success) {
            /* error */
            fprintf(stderr, "ERROR: Failed to parse json string");
            /* free json object */
            json_object_put(json);
            /* return */
            return;
        }

        json_bool succ;
        struct json_object *obj_main, *obj_temp;

        succ = json_object_object_get_ex(json, "main", &obj_main);
        if (succ) {
            succ = json_object_object_get_ex(obj_main, "temp", &obj_temp);
        }
        if (succ) {
	    // Temperature read is in Kelvins
            double outside = json_object_get_double(obj_temp);
	    temp->outside = outside-273.15;
        }

        /* free json object before return */
        json_object_put(json);
}

// Read the themometer 
static void get_thermometers(struct thermometers *temp)
{
	update_inside_temperature(temp);
	update_outside_temperature(temp);
}

// Display the temperature
static void display_thermometers(struct thermometers *temp)
{
	// First two indicators show the inside temperature
        display_pos(1, temp->inside/10);
        display_pos(2, temp->inside%10);

	// Indicators 5 and 6 show the outside temerature
	int outside = temp->outside;
	if (outside < 0) {
	    outside = - outside;
	}
        display_pos(5, outside/10);
        display_pos(6, outside%10);
}

int main()
{
    ws2811_return_t ret;
    int i;
    struct thermometers temp = {0, 0};

    ws2811_t ledstring =
    {
    	.freq = TARGET_FREQ,
    	.dmanum = DMA,
    	.channel =
    	{
            [0] =
        	{
                .gpionum = GPIO_PIN,
                .count = LED_COUNT,
                .invert = 0,
                .brightness = 255,
                .strip_type = STRIP_TYPE,
            },
            [1] =
            {
                .gpionum = 0,
                .count = 0,
                .invert = 0,
                .brightness = 0,
            },
        },
    };

    setup_handlers();

    // Initialize led backligh
    if ((ret = ws2811_init(&ledstring)) != WS2811_SUCCESS) {
        fprintf(stderr, "ws2811_init failed: %s\n", ws2811_get_return_t_str(ret));
        return ret;
    }

    // Initialize output pins.
    wiringPiSetup();

    pinMode(10, OUTPUT); // GPIO8
    pinMode(12, OUTPUT); // GPIO10
    pinMode(13, OUTPUT); // GPIO9
    pinMode(14, OUTPUT); // GPIO11

    pinMode(11, OUTPUT); // GPIO7
    pinMode(22, OUTPUT); // GPIO6

    pinMode(21, OUTPUT); // GPIO5
    pinMode(7, OUTPUT);  // GPIO4
    pinMode(9, OUTPUT);  // GPIO3
    pinMode(8, OUTPUT);  // GPIO2

    // Run all the digits on startup
    for (i=0; i<10; i++) {
        run_all_digits();
    }
    set_digit(0);

    // Read the termometers at startup.
    get_thermometers(&temp);

    for (i=0; running; i++) {
	struct tm *tm;
	struct timeval tv;

	// Read the current time
	gettimeofday(&tv, NULL);
	tm = localtime(&tv.tv_sec);

	if (tm->tm_min%5 == 1 && tm->tm_sec==tm->tm_min) {
	    // Every 5 minutes run all the digits on all idicators for 1 second.
            run_all_digits();
	} else if (tm->tm_min == 1 && tm->tm_sec==2) {
            // Every hour read the termometers
	    get_thermometers(&temp);
	} else if (tm->tm_min%3==1 && tm->tm_sec>tm->tm_min && tm->tm_sec<=tm->tm_min+3) {
            // Every 5 minutes display the temometer readings.
	    display_thermometers(&temp);
        } else {
	    // By defalt show current time.
            display_time(tm);
	    display_bars(&tv);
	    display_dots(&tv);
	}

        if (i%4==0) {
            // Update led backlight.
            display_leds(&ledstring);
	}
    }

    if (clear_on_exit) {
	matrix_clear(&ledstring);
	ws2811_render(&ledstring);
    }

    ws2811_fini(&ledstring);

    printf ("\n");
    return ret;
}
