/*
 * SPDX-FileCopyrightText: 2026 권경환 Kyunghwan Kwon <k@libmcu.org>
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef CURL_CURL_H
#define CURL_CURL_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef int64_t curl_off_t;
typedef int CURLcode;
typedef int CURLoption;
typedef int CURLINFO;

typedef struct CURL CURL;

typedef struct curl_slist {
	char *data;
	struct curl_slist *next;
} curl_slist;

typedef size_t (*curl_write_callback)(char *ptr, size_t size, size_t nmemb,
		void *userdata);

#define CURLE_OK			0
#define CURLE_OUT_OF_MEMORY		27
#define CURLE_OPERATION_TIMEDOUT	28

#define CURLOPT_WRITEDATA		10001
#define CURLOPT_URL			10002
#define CURLOPT_POSTFIELDS		10015
#define CURLOPT_HTTPHEADER		10023
#define CURLOPT_WRITEFUNCTION		20011
#define CURLOPT_POST			47
#define CURLOPT_NOSIGNAL		99
#define CURLOPT_TIMEOUT_MS		155
#define CURLOPT_POSTFIELDSIZE_LARGE	30120

#define CURLINFO_RESPONSE_CODE		0x200002

CURL *curl_easy_init(void);
CURLcode curl_easy_setopt(CURL *curl, CURLoption option, ...);
CURLcode curl_easy_perform(CURL *curl);
CURLcode curl_easy_getinfo(CURL *curl, CURLINFO info, ...);
void curl_easy_cleanup(CURL *curl);

struct curl_slist *curl_slist_append(struct curl_slist *list, const char *data);
void curl_slist_free_all(struct curl_slist *list);

void curl_mock_reset(void);
void curl_mock_set_init_handle(CURL *handle);
void curl_mock_set_perform_result(CURLcode result);
void curl_mock_set_getinfo_result(CURLcode result);
void curl_mock_set_response_code(long code);
void curl_mock_inject_response(const void *data, size_t len);
const char *curl_mock_last_url(void);
long curl_mock_last_timeout_ms(void);
long curl_mock_last_post_enabled(void);
long curl_mock_last_nosignal(void);
const void *curl_mock_last_post_fields(void);
curl_off_t curl_mock_last_post_size(void);
curl_write_callback curl_mock_last_write_callback(void);
void *curl_mock_last_write_data(void);
struct curl_slist *curl_mock_last_headers(void);
int curl_mock_cleanup_call_count(void);

#ifdef __cplusplus
}
#endif

#endif /* CURL_CURL_H */
