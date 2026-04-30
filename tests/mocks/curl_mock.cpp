/*
 * SPDX-FileCopyrightText: 2026 권경환 Kyunghwan Kwon <k@libmcu.org>
 *
 * SPDX-License-Identifier: MIT
 */

#include <stdarg.h>
#include <stdlib.h>
#include <string.h>

extern "C" {
#include "curl/curl.h"
}

struct CURL {
	int reserved;
};

static CURL g_default_handle;

static struct {
	CURL *init_handle;
	CURLcode global_init_result;
	CURL *last_handle;
	CURLcode perform_result;
	CURLcode getinfo_result;
	long response_code;
	char response_buf[8192];
	size_t response_len;
	const char *url;
	long timeout_ms;
	long post_enabled;
	long nosignal;
	const void *post_fields;
	curl_off_t post_size;
	curl_write_callback write_cb;
	void *write_data;
	struct curl_slist *headers_snapshot;
	int cleanup_call_count;
	int global_init_call_count;
	int global_cleanup_call_count;
} g_state;

static char *duplicate_string(const char *data);

static void free_slist(struct curl_slist *list)
{
	while (list != NULL) {
		struct curl_slist *next = list->next;

		free(list->data);
		free(list);
		list = next;
	}
}

static char *duplicate_string(const char *data)
{
	size_t len = strlen(data) + 1u;
	char *copy = (char *)malloc(len);

	if (copy == NULL) {
		return NULL;
	}

	memcpy(copy, data, len);

	return copy;
}

static struct curl_slist *clone_slist(const struct curl_slist *list)
{
	struct curl_slist *head = NULL;
	struct curl_slist *tail = NULL;

	while (list != NULL) {
		struct curl_slist *node = (struct curl_slist *)malloc(sizeof(*node));

		if (node == NULL) {
			free_slist(head);
			return NULL;
		}

		node->data = duplicate_string(list->data);
		if (node->data == NULL) {
			free(node);
			free_slist(head);
			return NULL;
		}
		node->next = NULL;

		if (head == NULL) {
			head = node;
		} else {
			tail->next = node;
		}
		tail = node;
		list = list->next;
	}

	return head;
}

extern "C" void curl_mock_reset(void)
{
	if (g_state.headers_snapshot != NULL) {
		free_slist(g_state.headers_snapshot);
	}

	memset(&g_state, 0, sizeof(g_state));
	g_state.init_handle = &g_default_handle;
	g_state.global_init_result = CURLE_OK;
	g_state.perform_result = CURLE_OK;
	g_state.getinfo_result = CURLE_OK;
	g_state.response_code = 200;
}

extern "C" void curl_mock_set_init_handle(CURL *handle)
{
	g_state.init_handle = handle;
}

extern "C" void curl_mock_set_perform_result(CURLcode result)
{
	g_state.perform_result = result;
}

extern "C" void curl_mock_set_getinfo_result(CURLcode result)
{
	g_state.getinfo_result = result;
}

extern "C" void curl_mock_set_global_init_result(CURLcode result)
{
	g_state.global_init_result = result;
}

extern "C" void curl_mock_set_response_code(long code)
{
	g_state.response_code = code;
}

extern "C" void curl_mock_inject_response(const void *data, size_t len)
{
	if (len > sizeof(g_state.response_buf)) {
		len = sizeof(g_state.response_buf);
	}

	if (len > 0u) {
		memcpy(g_state.response_buf, data, len);
	}
	g_state.response_len = len;
}

extern "C" const char *curl_mock_last_url(void)
{
	return g_state.url;
}

extern "C" long curl_mock_last_timeout_ms(void)
{
	return g_state.timeout_ms;
}

extern "C" long curl_mock_last_post_enabled(void)
{
	return g_state.post_enabled;
}

extern "C" long curl_mock_last_nosignal(void)
{
	return g_state.nosignal;
}

extern "C" const void *curl_mock_last_post_fields(void)
{
	return g_state.post_fields;
}

extern "C" curl_off_t curl_mock_last_post_size(void)
{
	return g_state.post_size;
}

extern "C" curl_write_callback curl_mock_last_write_callback(void)
{
	return g_state.write_cb;
}

extern "C" void *curl_mock_last_write_data(void)
{
	return g_state.write_data;
}

extern "C" struct curl_slist *curl_mock_last_headers(void)
{
	return g_state.headers_snapshot;
}

extern "C" int curl_mock_cleanup_call_count(void)
{
	return g_state.cleanup_call_count;
}

extern "C" int curl_mock_global_init_call_count(void)
{
	return g_state.global_init_call_count;
}

extern "C" int curl_mock_global_cleanup_call_count(void)
{
	return g_state.global_cleanup_call_count;
}

extern "C" CURLcode curl_global_init(long flags)
{
	(void)flags;
	g_state.global_init_call_count++;
	return g_state.global_init_result;
}

extern "C" CURLcode curl_global_sslset(long id, const char *name,
		const void *avail)
{
	(void)id;
	(void)name;
	(void)avail;
	return CURLE_OK;
}

extern "C" CURLcode curl_global_trace(const char *config)
{
	(void)config;
	return CURLE_OK;
}

extern "C" CURLcode curl_global_cleanup(void)
{
	g_state.global_cleanup_call_count++;
	return CURLE_OK;
}

extern "C" CURL *curl_easy_init(void)
{
	g_state.last_handle = g_state.init_handle;

	return g_state.init_handle;
}

extern "C" CURLcode curl_easy_setopt(CURL *curl, CURLoption option, ...)
{
	va_list ap;

	g_state.last_handle = curl;

	va_start(ap, option);
	switch (option) {
	case CURLOPT_URL:
		g_state.url = va_arg(ap, const char *);
		break;
	case CURLOPT_HTTPHEADER:
		if (g_state.headers_snapshot != NULL) {
			free_slist(g_state.headers_snapshot);
			g_state.headers_snapshot = NULL;
		}
		g_state.headers_snapshot = clone_slist(
				va_arg(ap, struct curl_slist *));
		break;
	case CURLOPT_POSTFIELDS:
		g_state.post_fields = va_arg(ap, const void *);
		break;
	case CURLOPT_POSTFIELDSIZE_LARGE:
		g_state.post_size = va_arg(ap, curl_off_t);
		break;
	case CURLOPT_TIMEOUT_MS:
		g_state.timeout_ms = va_arg(ap, long);
		break;
	case CURLOPT_WRITEFUNCTION:
		g_state.write_cb = va_arg(ap, curl_write_callback);
		break;
	case CURLOPT_WRITEDATA:
		g_state.write_data = va_arg(ap, void *);
		break;
	case CURLOPT_POST:
		g_state.post_enabled = va_arg(ap, long);
		break;
	case CURLOPT_NOSIGNAL:
		g_state.nosignal = va_arg(ap, long);
		break;
	default:
		(void)va_arg(ap, void *);
		break;
	}
	va_end(ap);

	return CURLE_OK;
}

extern "C" CURLcode curl_easy_perform(CURL *curl)
{
	g_state.last_handle = curl;

	if (g_state.perform_result == CURLE_OK && g_state.write_cb != NULL
			&& g_state.response_len > 0u) {
		g_state.write_cb(g_state.response_buf, 1u, g_state.response_len,
				g_state.write_data);
	}

	return g_state.perform_result;
}

extern "C" CURLcode curl_easy_getinfo(CURL *curl, CURLINFO info, ...)
{
	va_list ap;
	long *out;

	g_state.last_handle = curl;

	va_start(ap, info);
	out = va_arg(ap, long *);
	va_end(ap);

	if (info == CURLINFO_RESPONSE_CODE && out != NULL) {
		*out = g_state.response_code;
	}

	return g_state.getinfo_result;
}

extern "C" void curl_easy_cleanup(CURL *curl)
{
	g_state.last_handle = curl;
	g_state.cleanup_call_count++;
}

extern "C" struct curl_slist *curl_slist_append(struct curl_slist *list,
		const char *data)
{
	struct curl_slist *node = (struct curl_slist *)malloc(sizeof(*node));
	struct curl_slist *tail = list;

	if (node == NULL) {
		return NULL;
	}

	node->data = duplicate_string(data);
	if (node->data == NULL) {
		free(node);
		return NULL;
	}
	node->next = NULL;

	if (list == NULL) {
		return node;
	}

	while (tail->next != NULL) {
		tail = tail->next;
	}
	tail->next = node;

	return list;
}

extern "C" void curl_slist_free_all(struct curl_slist *list)
{
	free_slist(list);
}
