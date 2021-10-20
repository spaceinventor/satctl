/*
 * hk_collector.c
 *
 *  Created on: Oct 19, 2021
 *      Author: Mads
 */

#include "hk_collector.h"

#include <csp/csp.h>
#include <csp/arch/csp_thread.h>
#include <csp/arch/csp_clock.h>
#include <csp/arch/csp_time.h>

#include <param/param.h>
#include <param/param_client.h>
#include <param/param_list.h>
#include <param/param_server.h>

#include <sys/types.h>

#include <stdio.h>
#include <string.h>
#include <param_config.h>

struct hk_collector_config_s hk_collector_config[16] = {0};

typedef void (*param_transaction_callback_f)(csp_packet_t *response, int verbose, int version);
int param_transaction(csp_packet_t *packet, int host, int timeout, param_transaction_callback_f callback, int verbose, int version);
void param_transaction_callback_collect(csp_packet_t *response, int verbose, int version);

void hk_col_confstr_callback(struct param_s * param, int offset) {
	hk_collector_init();
}

extern const vmem_t vmem_hk_col;
PARAM_DEFINE_STATIC_VMEM(PARAMID_HK_COLLECTOR_RUN, hk_col_run, PARAM_TYPE_UINT8, 0, sizeof(uint8_t), PM_CONF, NULL, "", hk_col, 0x0, NULL);
PARAM_DEFINE_STATIC_VMEM(PARAMID_HK_COLLECTOR_VERBOSE, hk_col_verbose, PARAM_TYPE_UINT8, 0, sizeof(uint8_t), PM_CONF, NULL, "", hk_col, 0x1, NULL);
PARAM_DEFINE_STATIC_VMEM(PARAMID_HK_COLLECTOR_CNFSTR, hk_col_cnfstr, PARAM_TYPE_STRING, 100, 0, PM_CONF, hk_col_confstr_callback, "", hk_col, 0x02, NULL);

void hk_collector_init(void) {
	char buf[100];
	param_get_data(&hk_col_cnfstr, buf, 100);
	//int len = strnlen(buf, 100);
	//printf("Init with str: %s, len %u\n", buf, len);

	/* Clear config array */
	memset(hk_collector_config, 0, sizeof(hk_collector_config));

	/* Get first token */
	char *saveptr;
	char * str = strtok_r(buf, ",", &saveptr);
	int i = 0;

	while ((str) && (strlen(str) > 1)) {
		unsigned int node, interval, mask = 0xFFFFFFFF;
		if (sscanf(str, "%u %u %x", &node, &interval, &mask) != 3) {
			if (sscanf(str, "%u %u", &node, &interval) != 2) {
				printf("Parse error %s", str);
				return;
			}
		}
		//printf("Collect node %u each %u ms, mask %x\n", node, interval, mask);

		hk_collector_config[i].node = node;
		hk_collector_config[i].interval = interval;
		hk_collector_config[i].mask = mask;

		i++;
		str = strtok_r(NULL, ",", &saveptr);
	}

}

csp_thread_return_t hk_collector_task(void *pvParameters) {

	hk_collector_init();

	while(1) {

		csp_sleep_ms(100);

		for(int i = 0; i < 16; i++) {
			if (hk_collector_config[i].node == 0)
				break;

			if (param_get_uint8(&hk_col_run) == 0)
				continue;

			if (csp_get_ms() < hk_collector_config[i].last_time + hk_collector_config[i].interval) {
				continue;
			}

			hk_collector_config[i].last_time = csp_get_ms();

			csp_packet_t *packet = csp_buffer_get(PARAM_SERVER_MTU);
			if (packet == NULL)
				continue;
			
			packet->data[0] = PARAM_PULL_ALL_REQUEST_V2;
			packet->data[1] = 0;
			packet->data32[1] = htobe32(hk_collector_config[i].mask);
			packet->data32[2] = 0;
			packet->length = 12;
			
			param_transaction(packet, hk_collector_config[i].node, 1000, param_transaction_callback_collect, param_get_uint8(&hk_col_verbose), 2);

		}

	}

}
