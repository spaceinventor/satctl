/*
 * hk_collector.h
 *
 *  Created on: Oct 19, 2021
 *      Author: Mads
 */

#pragma once

#include <param/param.h>
#include <csp/arch/csp_thread.h>

struct hk_collector_config_s {
	uint8_t node;
	uint32_t interval;
	uint32_t mask;
	uint32_t last_time;
};

void hk_collector_init(void);

csp_thread_return_t hk_collector_task(void *pvParameters);
