/*
 * broadcast_client.c
 *
 *  Created on: Aug 28, 2018
 *      Author: johan
 */

#include <unistd.h>
#include <stdio.h>

#include <csp/csp.h>
#include <csp/csp_endian.h>

#include "broadcast_client.h"

#define BROADCAST_CHUNK_SIZE 192

static void broadcast_client(csp_packet_t * packet) {
	csp_hex_dump("Broadcast received", packet->data, packet->length);

	uint16_t chunk_id = csp_ntoh16(packet->data16[0]);
	uint16_t chunks_total = csp_ntoh16(packet->data16[1]);
	uint32_t file_id = csp_ntoh32(packet->data32[1]);

	printf("Chunk id %u/%u fileid %u\n", chunk_id, chunks_total, file_id);

	/* Create/open mapfile */
	char mapfile[100];
	snprintf(mapfile, 100, "%u.map", file_id);

	FILE * mapfile_fp = fopen(mapfile, "r+");
	if (!mapfile_fp) {
		mapfile_fp = fopen(mapfile, "w+");
		if (!mapfile_fp) {
			goto out_free;
		}
	}

	printf("Open: %s\n", mapfile);

	/* Check if the file is new */
	fseek(mapfile_fp, 0L, SEEK_END);
	if (chunks_total + 1 != ftell(mapfile_fp)) {
		rewind(mapfile_fp);
		for(int i = 0; i < chunks_total + 1; i++) {
			fputc('-', mapfile_fp);
		}
		printf("Clear mapfile\n");
	}

	/* Create/open datfile */
	char datfile[100];
	snprintf(datfile, 100, "%u.dat", file_id);
	FILE * datfile_fp = fopen(datfile, "r+");
	if (!datfile_fp) {
		datfile_fp = fopen(datfile, "w+");
		if (!datfile_fp) {
			goto out_close_map;
		}
	}

	printf("Open: %s\n", datfile);

	/* Write data */
	int result;
	result = fseek(datfile_fp, chunk_id * BROADCAST_CHUNK_SIZE, SEEK_SET);
	printf("Seek to %u, result %d\n", chunk_id * BROADCAST_CHUNK_SIZE, result);
	result = fwrite(&packet->data32[2], 1, packet->length - 8, datfile_fp);
	printf("Write %u bytes, result %d\n", packet->length - 8, result);
	fclose(datfile_fp);

	/* Write map */
	fseek(mapfile_fp, (long int) chunk_id, SEEK_SET);
	fputc('X', mapfile_fp);

out_close_map:
	fclose(mapfile_fp);
out_free:
	csp_buffer_free(packet);
}

void broadcast_client_init(void) {
	csp_socket_t *sock_broadcast_client = csp_socket(CSP_SO_NONE);
	csp_socket_set_callback(sock_broadcast_client, broadcast_client);
	csp_bind(sock_broadcast_client, 15);
}
