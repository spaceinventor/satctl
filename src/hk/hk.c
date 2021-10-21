/*
 * hk.c
 *
 *  Created on: Oct 15, 2021
 *      Author: Mads
 */

#include "hk.h"

#if HK_EMBED
#include <FreeRTOS.h>
#include <task.h>
#include "sd_mmc/sd_mmc.h"
#include "sd_mmc/sd_mmc_protocol.h"
#include <vmem_config.h>
#else
#include <vmem/vmem_file.h>
#endif

#include <sys/types.h>
#include <string.h>

#include <csp/csp.h>
#include <csp/arch/csp_thread.h>
#include <csp/arch/csp_time.h>
#include <csp/arch/csp_semaphore.h>

#include <param/param.h>
#include <param/param_client.h>
#include <param/param_queue.h>
#include <objstore/objstore.h>
#include "hk_collector.h"

#include <vmem/vmem.h>
#include <vmem/vmem_ram.h>
#include <vmem/vmem_fram.h>
#include <vmem/vmem_fram_secure.h>

#include <param_config.h>

// params to control what is logged ?
// a few mask sets and a setting for each node you want logging from to pick which set to apply to that node

//VMEM_DEFINE_FRAM(hk_col, "hk_col", VMEM_CONF_COL_FRAM, VMEM_CONF_COL_SIZE, VMEM_CONF_COL_VADDR);
VMEM_DEFINE_FILE(hk_col, "hk_col", "hk_col.cnf", 0x100);

/* RAM Buffers for write and read */
VMEM_DEFINE_STATIC_RAM(hk_wr0, "hk_wr0", HK_RAM_BUF_SIZE);
VMEM_DEFINE_STATIC_RAM(hk_wr1, "hk_wr1", HK_RAM_BUF_SIZE);
VMEM_DEFINE_STATIC_RAM(hk_rd0, "hk_rd0", HK_RAM_BUF_SIZE);
VMEM_DEFINE_STATIC_RAM(hk_rd1, "hk_rd1", HK_RAM_BUF_SIZE);

VMEM_DEFINE_FILE(hk_test, "hk_test", "hk_test", 16*HK_RAM_BUF_SIZE);

static csp_mutex_t hk_storage_mtx;
static csp_mutex_buffer_t hk_storage_mtx_buffer;

static uint8_t active_write_buf = 0;
static uint8_t active_read_buf = 0;

static param_queue_t temp_queue;

static int saved_buffers = 0;

static void hk_load(vmem_t * rd_buf, int block_addr) {
	if (csp_mutex_lock(&hk_storage_mtx, 1000) < 0) {
		// failed to get mutex - TODO: make it retry somehow
		return;
	}

	vmem_hk_test.read(&vmem_hk_test, block_addr * HK_RAM_BUF_SIZE, rd_buf->vaddr, HK_RAM_BUF_SIZE);
}

static vmem_t * hk_read(int block_addr) {
	vmem_t *rd_buf;
	if (active_read_buf == 0) {
		rd_buf = &vmem_hk_rd0;
	} else {
		rd_buf = &vmem_hk_rd1;
	}

	hk_load(rd_buf, block_addr);

	return rd_buf;
}

static void hk_save_start(vmem_t * wr_buf) {
	if (csp_mutex_lock(&hk_storage_mtx, 1000) < 0) {
		// failed to get mutex - TODO: make it retry somehow
		return;
	}

	#if HK_EMBED
	// TODO: eMMC write init + start
	#else
	int addr = saved_buffers * HK_RAM_BUF_SIZE;

	vmem_hk_test.write(&vmem_hk_test, addr, wr_buf->vaddr, HK_RAM_BUF_SIZE);

	saved_buffers++;
	#endif
}

static void hk_save_finish(vmem_t * wr_buf) {
	// wait for eMMC to finish writing

	csp_mutex_unlock(&hk_storage_mtx);
}

/* Write a queue to the RAM buffer */
static void hk_write(param_queue_t * queue_in) {
	vmem_t *wr_buf;
	if (active_write_buf == 0) {
		wr_buf = &vmem_hk_wr0;
	} else {
		wr_buf = &vmem_hk_wr1;
	}

	int obj_length = sizeof(*queue_in) - sizeof(queue_in->buffer) + queue_in->used;

	int obj_offset = objstore_alloc(wr_buf, obj_length, 0);
	if (obj_offset < 0) {
		/* Target buffer full, begin saving it and switch */
		hk_save_start(wr_buf);
		if (active_write_buf == 0) {
			wr_buf = &vmem_hk_wr1;
			active_write_buf = 1;
		} else {
			wr_buf = &vmem_hk_wr0;
			active_write_buf = 0;
		}

		/* Wait to make sure the new buffer is ready */
		hk_save_finish(wr_buf);
		
		/* Clear the buffer before reusing it */
		uint32_t zeros[256] = {0};
		for (int i = 0; i < HK_RAM_BUF_SIZE; i += 1024) {
			wr_buf->write(wr_buf, i, (void *) zeros, 1024);
		}

		/* Try allocating again */
		obj_offset = objstore_alloc(wr_buf, obj_length, 0);
		if (obj_offset < 0) {
			// Something is completely wrong or trying to go too fast, drop this queue
			return;
		}
	}

	/* Save the queue to the buffer */
	void * write_ptr = (void *) (long int) queue_in + sizeof(queue_in->buffer);
    objstore_write_obj(wr_buf, obj_offset, (uint8_t) OBJ_TYPE_HK_QUEUE, (uint8_t) obj_length, write_ptr);
}

/* Callback, run when collector receives a pull response */
void param_transaction_callback_collect(csp_packet_t *response, int verbose, int version) {
	param_queue_init(&temp_queue, &response->data[2], response->length - 2, response->length - 2, PARAM_QUEUE_TYPE_SET, version);
	temp_queue.last_node = response->id.src;

	/* Write data to local memory params */
	param_queue_apply(&temp_queue);

    /* Save it to the HK system */
	hk_write(&temp_queue);

	if (verbose) {
		/* Loop over paramid's in pull response */
		PARAM_QUEUE_FOREACH(param, reader, (&temp_queue), offset)

			/* We need to discard the data field, to get to next paramid */
			mpack_discard(&reader);

			/* Print the local RAM copy of the remote parameter */
			if (param) {
				param_print(param, -1, NULL, 0, 0);
			}
		}
	}

	csp_buffer_free(response);
	memset(&temp_queue, 0, sizeof(temp_queue));
}

/*
csp_thread_return_t hk_task(void) {

}
*/


void hk_init(void) {
	/* HK storage init */
	csp_mutex_create_static(&hk_storage_mtx, &hk_storage_mtx_buffer);
	#if HK_EMBED
	// TODO eMMC init
	#else
	vmem_file_init(&vmem_hk_test);
	#endif

    /* Collector config init and task setup */
    #if HK_EMBED
	vmem_fram_secure_init(&vmem_hk_col);
    static StaticTask_t hk_collector_task_tcb;
	static StackType_t hk_collector_task_stack[1000];
	xTaskCreateStatic(hk_collector_task, "COLLECTOR", 1000, NULL, 3, hk_collector_task_stack, &hk_collector_task_tcb);
	#else
	vmem_file_init(&vmem_hk_col);
	pthread_t hk_collector_handle;
	pthread_create(&hk_collector_handle, NULL, &hk_collector_task, NULL);
	#endif

}
