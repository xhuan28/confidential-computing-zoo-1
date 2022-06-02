/* SPDX-License-Identifier: LGPL-3.0-or-later */
/* Copyright (C) 2020 Intel Labs */

#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include "secret_prov.h"
#include "cross_comm.h"
#include "clf_server.h"

#define WRAP_KEY_SIZE	16
#define MRSIGNER_LEN	32
#define MRENCLAVE_LEN	32

static pthread_mutex_t g_print_lock;
char g_secret_pf_key_hex[WRAP_KEY_SIZE * 2 + 1] = {0};

#define MR_LEN		32
char g_mrenclave[MR_LEN] = {0};
char g_mrsigner[MR_LEN] = {0};
uint16_t g_isv_prod_id = 0;
uint16_t g_isv_svn = 0;

int communicate_with_client_callback(struct ra_tls_ctx* ctx);

static void hexdump_mem(const void* data, size_t size) {
	uint8_t* ptr = (uint8_t*)data;
	for (size_t i = 0; i < size; i++)
		printf("%02x", ptr[i]);
	printf("\n");
}

/*
 * specific callback to verify SGX measurements during TLS handshake
 */
static int verify_measurements_callback(const char* mrenclave, const char* mrsigner,
                                        const char* isv_prod_id, const char* isv_svn) {
	int ret = -1;	//error must be a negative value

	assert(mrenclave && mrsigner && isv_prod_id && isv_svn);

	pthread_mutex_lock(&g_print_lock);
	puts("Received the following measurements from the client:");
	printf("  - MRENCLAVE:   "); hexdump_mem(mrenclave, 32);
	printf("  - MRSIGNER:    "); hexdump_mem(mrsigner, 32);
	printf("  - ISV_PROD_ID: %hu\n", *((uint16_t*)isv_prod_id));
	printf("  - ISV_SVN:     %hu\n", *((uint16_t*)isv_svn));
	pthread_mutex_unlock(&g_print_lock);

	char null_mrenclave[MRENCLAVE_LEN] = {0};
	char null_mrsigner[MRSIGNER_LEN] = {0};
	if(memcmp(g_mrenclave, null_mrenclave, MRENCLAVE_LEN)) {
		if(memcmp(g_mrenclave, mrenclave, MRENCLAVE_LEN)) {
			printf("mrenclave mismatch\n");
			return ret;
		}
	}
	if(memcmp(g_mrsigner, null_mrsigner, MRSIGNER_LEN)) {
		if(memcmp(g_mrsigner, mrsigner, MRSIGNER_LEN)) {
			printf("mrsigner mismatch\n");
			return ret;
		}
	}
	if(g_isv_prod_id!=0 && g_isv_prod_id!=*((uint16_t*)isv_prod_id)) {
		printf("isv_prod_id mismatch\n");
		return ret;
	}
	if(g_isv_svn!=0 && g_isv_svn!=*((uint16_t*)isv_svn)){
		printf("isv_svn mismatch\n");
		return ret;
	}

	return 0;
}

int main(int argc, char** argv) {
	int ret;

	ret = pthread_mutex_init(&g_print_lock, NULL);
	if (ret < 0)
		return ret;

	int len = 0;
	char szVal[72];
	const char conf[] = "clf_server.conf";
	read_config(conf, "MRSigner", szVal, MRSIGNER_LEN, &len);
	printf("MRSigner=%s\n", szVal);
	hexstr2buff(szVal, g_mrsigner, MRSIGNER_LEN);
	dump_buff(g_mrsigner, MRSIGNER_LEN);

	read_config(conf, "MREnclave", szVal, MRENCLAVE_LEN, &len);
	printf("MREnclave=%s\n", szVal);
	hexstr2buff(szVal, g_mrenclave, MRENCLAVE_LEN);
	dump_buff(g_mrenclave, MRENCLAVE_LEN);

	read_config_short(conf, "isv_prod_id", (int16_t*)&g_isv_prod_id);
	printf("isv_prod_id=%d\n", g_isv_prod_id);

	read_config_short(conf, "isv_svn", (int16_t*)&g_isv_svn);
	printf("isv_svn=%d\n", g_isv_svn);

	/*TODO: load secret from user specified file or HSM */
	//uint8_t ptr[16] = {0};
	time_t t;
	srand((unsigned) time(&t));
	for (size_t i = 0; i < 16; i++)
		sprintf(&g_secret_pf_key_hex[i * 2], "%02x", rand()%0xff);

	puts("Starting server on port 4433.");
	/*TODO: make port/certification configurable */
	ret = secret_provision_start_server((uint8_t*)g_secret_pf_key_hex, sizeof(g_secret_pf_key_hex),
										"4433",
										"certs/server_signed_cert.crt", "certs/server_private_key.pem",
										verify_measurements_callback,
										communicate_with_client_callback);
	if (ret < 0) {
		fprintf(stderr, "[error] secret_provision_start_server() returned %d\n", ret);
		return 1;
	}

	pthread_mutex_destroy(&g_print_lock);
	return 0;
}
