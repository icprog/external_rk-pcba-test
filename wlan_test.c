/*
 * Copyright (C) 2007 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#define LOG_TAG "wltest"

#include <errno.h>
#include <sys/types.h>
#include <sys/uio.h>
#include <sys/stat.h>
#include <sys/time.h>  // for utimes
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <utime.h>
#include <fcntl.h>

#include <cutils/log.h>
#include "common.h"
#include "extra-functions.h"
#include "wlan_test.h"
#include "test_case.h"
#include "language.h"

#define LOG(x...) printf(x)

#define MAX_SCAN_COUNTS (64)
#define SCAN_RESULT_LENGTH (128*MAX_SCAN_COUNTS)
#define SCAN_RESULT_FILE "/data/scan_result.txt"
#define SCAN_RESULT_FILE2 "/data/scan_result2.txt"

char ssids[MAX_SCAN_COUNTS][128];
char rssis[MAX_SCAN_COUNTS][128];

/* RSSI Levels as used by notification icon
   Level 4  -55 <= RSSI
   Level 3  -66 <= RSSI < -55
   Level 2  -77 <= RSSI < -67
   Level 1  -88 <= RSSI < -78
   Level 0         RSSI < -88 */
int calcSingleLevel(int rssi)
{
	rssi *= -1;
	if(rssi >= -55) {
		return 4;
	} else if(rssi >= -66) {
		return 3;
	} else if(rssi >= -77) {
		return 2;
	} else if(rssi >= -88) {
		return 1;
	} else {
		return 0;
	}
}

void process_ssid(char *dst, char *src, char *src2)
{
	char *p, *p2, *tmp, *tmp2;
	int i, j, dbm, dbm2 = 99, index = 0, rssi;
	
	for(i = 0; i < MAX_SCAN_COUNTS; i++) {
		
		// ESSID:"PocketAP_Home"
		tmp = &ssids[i][0];
		p = strstr(src, "ESSID:");
		if(p == NULL) {
			break;
		}
		// skip "ESSID:"
		p += strlen("ESSID:");
		while((*p != '\0') && (*p != '\n')) {
			*tmp++ = *p++;
		}
		*tmp++ = '\0';
		src = p;
		//LOG("src = %s\n", src);
		
		// Quality:4/5  Signal level:-59 dBm  Noise level:-96 dBm
		tmp2 = &rssis[i][0];
		p2 = strstr(src2, "Signal level");
		if(p2 == NULL) {
			break;
		}
		// skip "level="
		p2 += strlen("Signal level") + 1;
		// like "-90 dBm", total 3 chars
		*tmp2++ = *p2++; //'-'
		*tmp2++ = *p2++; //'9'
		*tmp2++ = *p2++; //'0'
		*tmp2++ = *p2++; //' '
		*tmp2++ = *p2++; //'d'
		*tmp2++ = *p2++; //'B'
		*tmp2++ = *p2++; //'m'
		*tmp2++ = '\0';
		src2 = p2;
		//LOG("src2 = %s\n", src2);
		
		LOG("i = %d, %s, %s\n", i, &ssids[i][0], &rssis[i][0]);
	}
	
	LOG("total = %d\n", i);
	if(i == 0) {
		return;
	}
	
	for(j = 0; j < i; j++) {
		dbm = atoi(&rssis[j][1]); // skip '-'
                if(dbm == 0) continue;
		if(dbm < dbm2) { // get max rssi
			dbm2 = dbm;
			index = j;
		}
	}
	
	LOG("index = %d, dbm = %d\n", index, dbm2);
	LOG("select ap: %s, %s\n", &ssids[index][0], &rssis[index][0]);
	
	rssi = calcSingleLevel(atoi(&rssis[index][1]));
	
	//sprintf(dst, "{ %s %s %d %s }", &ssids[index][0], PCBA_WIFI_SIGNAL, rssi, PCBA_WIFI_SIGNAL1);
	sprintf(dst, "{ %s \"%d\" }", &ssids[index][0], rssi);
}

// ---------------------------------------------------------------------------

void* wlan_test(void* argv)
{
	int ret,y;
	FILE *fp = NULL;
	FILE *fp2 = NULL;
	char *results = NULL;
	char *results2 = NULL;
	char ssid[100];
	struct testcase_info *tc_info = (struct testcase_info *)argv;
	char wifi_pcba_node = 1;

	/*remind ddr test*/
	if(tc_info->y <= 0)
		tc_info->y  = get_cur_print_y();	

	y = tc_info->y;
	ui_print_xy_rgba(0,y,255,255,0,255,"%s:[%s..] \n",PCBA_WIFI,PCBA_TESTING);
	
	ret =  __system("busybox chmod 777 /res/wifi.sh");
	if(ret)
		LOG("chmod wifi.sh failed :%d\n",ret);
		
	ret = __system("/res/wifi.sh");
	if(ret <= 0) {
		LOG("wifi test failed.\n");
		goto error_exit;
	}
	
	results = malloc(SCAN_RESULT_LENGTH);
	if(results == NULL) {
		LOG("can malloc results buffer.\n");
		goto error_exit;
	}
	
	results2 = malloc(SCAN_RESULT_LENGTH);
	if(results2 == NULL) {
		LOG("can malloc results2 buffer.\n");
		goto error_exit;
	}
	
	fp = fopen(SCAN_RESULT_FILE, "r");
	if(fp == NULL) {
		LOG("can not open %s.\n", SCAN_RESULT_FILE);
		goto error_exit;
	}
	memset(results, 0, SCAN_RESULT_LENGTH);
	fread(results,SCAN_RESULT_LENGTH,1,fp);
	results[SCAN_RESULT_LENGTH-1] = '\0';
	//LOG("%s.\n", results);
	
	fp2 = fopen(SCAN_RESULT_FILE2, "r");
	if(fp2 == NULL) {
		LOG("can not open %s.\n", SCAN_RESULT_FILE2);
		goto error_exit;
	}
	memset(results2, 0, SCAN_RESULT_LENGTH);
	fread(results2,SCAN_RESULT_LENGTH,1,fp2);
	results2[SCAN_RESULT_LENGTH-1] = '\0';
  //LOG("%s.\n", results2);
	
	memset(ssid, 0, 100);
	
	process_ssid(ssid, results, results2);
	ui_print_xy_rgba(0,y,0,255,0,255,"%s:[%s] %s\n",PCBA_WIFI,PCBA_SECCESS,ssid);
	
	LOG("wlan_test success.\n");
	return 0;

error_exit:
	
	LOG("wlan_test failed.\n");
	
	if(fp != NULL) {
		fclose(fp);
	}
	
	if(fp2 != NULL) {
		fclose(fp2);
	}
	
	if(results != NULL) {
		free(results);
	}
	
	if(results2 != NULL) {
		free(results2);
	}
	
	ui_print_xy_rgba(0,y,225,0,0,255,"%s:[%s] %s\n",PCBA_WIFI,PCBA_FAILED,ssid);
	tc_info->result = -1;
		
  return argv;
}
