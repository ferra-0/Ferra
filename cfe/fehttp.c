#if !defined(_WIN32)
#define _POSIX_C_SOURCE 200809L
#endif

#include <curl/curl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <errno.h>
#include <pthread.h>
#include <time.h>
#endif

#define HTTP_CONNECT_TIMEOUT_SECONDS 10L
#define HTTP_REQUEST_TIMEOUT_SECONDS 120L
#define HTTP_GET_MAX_ATTEMPTS 3
#define HTTP_RETRY_BASE_MILLISECONDS 250L
#define HTTP_DOH_FALLBACK_URL "https://1.1.1.1/dns-query"

struct Buffer {
	char* data;
	size_t size;
};

struct HttpStream {
	CURL* easy;
	CURLM* multi;
	unsigned char* pending;
	size_t pending_size;
	size_t pending_offset;
	size_t pending_limit;
	int paused;
	int transfer_done;
	CURLcode result;
	long status;
	char error_buffer[CURL_ERROR_SIZE];
};

static void initialize_curl(void) {
	curl_global_init(CURL_GLOBAL_DEFAULT);
}

#if defined(_WIN32)
static INIT_ONCE curl_once = INIT_ONCE_STATIC_INIT;

static BOOL CALLBACK initialize_curl_once(
	PINIT_ONCE once,
	PVOID parameter,
	PVOID* context
) {
	(void)once;
	(void)parameter;
	(void)context;
	initialize_curl();
	return TRUE;
}

static void ensure_curl_initialized(void) {
	InitOnceExecuteOnce(&curl_once, initialize_curl_once, NULL, NULL);
}
#else
static pthread_once_t curl_once = PTHREAD_ONCE_INIT;

static void ensure_curl_initialized(void) {
	pthread_once(&curl_once, initialize_curl);
}
#endif

void http_cleanup(void) {
	/* Kept for source and ABI compatibility. Each request owns its handle. */
}

static int reset_buffer(struct Buffer* buf) {
	free(buf->data);
	buf->data = malloc(1);
	buf->size = 0;

	if (!buf->data)
		return 0;

	buf->data[0] = '\0';
	return 1;
}

static int is_transient_error(CURLcode code) {
	switch (code) {
		case CURLE_COULDNT_RESOLVE_HOST:
		case CURLE_COULDNT_CONNECT:
		case CURLE_OPERATION_TIMEDOUT:
		case CURLE_SSL_CONNECT_ERROR:
		case CURLE_GOT_NOTHING:
		case CURLE_SEND_ERROR:
		case CURLE_RECV_ERROR:
			return 1;
		default:
			return 0;
	}
}

static void retry_delay(int attempt) {
	long milliseconds = HTTP_RETRY_BASE_MILLISECONDS << attempt;
#if defined(_WIN32)
	Sleep((DWORD)milliseconds);
#else
	struct timespec remaining = {
		milliseconds / 1000L,
		(milliseconds % 1000L) * 1000000L
	};

	while (nanosleep(&remaining, &remaining) != 0 && errno == EINTR) {
	}
#endif
}

static size_t write_callback(
	void* contents,
	size_t size,
	size_t nmemb,
	void* userp
) {
	size_t total = size * nmemb;

	struct Buffer* buf = userp;

	char* p = realloc(
		buf->data,
		buf->size + total + 1
	);

	if (!p) return 0;

	buf->data = p;

	memcpy(
		buf->data + buf->size,
		contents,
		total
	);

	buf->size += total;
	buf->data[buf->size] = '\0';

	return total;
}

static CURLcode perform_request_attempt(
	const char* url,
	const char* method,
	const char* body,
	struct curl_slist* header_list,
	struct Buffer* buf,
	char error_buffer[CURL_ERROR_SIZE],
	int attempt
) {
	ensure_curl_initialized();
	error_buffer[0] = '\0';
	CURL* curl = curl_easy_init();

	if (!curl)
		return CURLE_FAILED_INIT;

	curl_easy_setopt(curl, CURLOPT_URL, url);
	curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback);
	curl_easy_setopt(curl, CURLOPT_WRITEDATA, buf);
	curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
	curl_easy_setopt(curl, CURLOPT_TCP_KEEPALIVE, 1L);
	curl_easy_setopt(curl, CURLOPT_TCP_KEEPIDLE, 30L);
	curl_easy_setopt(curl, CURLOPT_TCP_KEEPINTVL, 15L);

	/*
	 * Some systems publish AAAA records despite having no usable IPv6 default
	 * route. Keep the normal dual-stack path first, then make retries use IPv4.
	 */
	if (attempt > 0)
		curl_easy_setopt(curl, CURLOPT_IPRESOLVE, CURL_IPRESOLVE_V4);

	/*
	 * The final GET attempt bypasses a failing local DNS stub via DNS-over-HTTPS.
	 * Its endpoint is an IP literal, so reaching it does not need system DNS.
	 */
	if (attempt > 1)
		curl_easy_setopt(curl, CURLOPT_DOH_URL, HTTP_DOH_FALLBACK_URL);

	/*
	 * A server-side long-poll timeout is not the whole request duration:
	 * DNS, TCP and TLS also consume time. Keeping libcurl at the same
	 * 30-second limit as Telegram's `timeout=30` races the response.
	 */
	curl_easy_setopt(
		curl,
		CURLOPT_CONNECTTIMEOUT,
		HTTP_CONNECT_TIMEOUT_SECONDS
	);
	curl_easy_setopt(
		curl,
		CURLOPT_TIMEOUT,
		HTTP_REQUEST_TIMEOUT_SECONDS
	);
	curl_easy_setopt(curl, CURLOPT_ERRORBUFFER, error_buffer);

	if (header_list)
		curl_easy_setopt(curl, CURLOPT_HTTPHEADER, header_list);

	if (strcmp(method, "POST") == 0) {
		curl_easy_setopt(curl, CURLOPT_POST, 1L);
		curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body ? body : "");
		curl_easy_setopt(
			curl,
			CURLOPT_POSTFIELDSIZE,
			body ? (long)strlen(body) : 0L
		);
	}

	CURLcode result = curl_easy_perform(curl);
	curl_easy_cleanup(curl);
	return result;
}

static size_t stream_write_callback(
	void* contents,
	size_t size,
	size_t nmemb,
	void* userp
) {
	struct HttpStream* stream = userp;
	size_t total = size * nmemb;
	if (total == 0)
		return 0;

	if (stream->pending_offset > 0) {
		size_t available = stream->pending_size - stream->pending_offset;
		if (available > 0) {
			memmove(
				stream->pending,
				stream->pending + stream->pending_offset,
				available
			);
		}
		stream->pending_size = available;
		stream->pending_offset = 0;
	}
	if (stream->pending_limit > 0 &&
			stream->pending_size >= stream->pending_limit) {
		stream->paused = 1;
		return CURL_WRITEFUNC_PAUSE;
	}

	if (total > SIZE_MAX - stream->pending_size)
		return 0;
	unsigned char* grown = realloc(
		stream->pending,
		stream->pending_size + total
	);
	if (!grown)
		return 0;
	stream->pending = grown;
	memcpy(stream->pending + stream->pending_size, contents, total);
	stream->pending_size += total;
	return total;
}

static void http_stream_collect_messages(struct HttpStream* stream) {
	int messages_left = 0;
	CURLMsg* message = NULL;
	while ((message = curl_multi_info_read(stream->multi, &messages_left))) {
		if (message->msg != CURLMSG_DONE || message->easy_handle != stream->easy)
			continue;
		stream->transfer_done = 1;
		stream->result = message->data.result;
		curl_easy_getinfo(stream->easy, CURLINFO_RESPONSE_CODE, &stream->status);
	}
}

static int http_stream_pump(struct HttpStream* stream) {
	while (!stream->transfer_done &&
			stream->pending_offset == stream->pending_size) {
		int running = 0;
		CURLMcode multi_result = curl_multi_perform(stream->multi, &running);
		if (multi_result != CURLM_OK) {
			stream->transfer_done = 1;
			stream->result = CURLE_RECV_ERROR;
			snprintf(
				stream->error_buffer,
				sizeof(stream->error_buffer),
				"curl_multi_perform: %s",
				curl_multi_strerror(multi_result)
			);
			return 0;
		}

		http_stream_collect_messages(stream);
		if (stream->transfer_done ||
				stream->pending_offset < stream->pending_size)
			break;

		int descriptors = 0;
		multi_result = curl_multi_poll(stream->multi, NULL, 0, 1000, &descriptors);
		if (multi_result != CURLM_OK) {
			stream->transfer_done = 1;
			stream->result = CURLE_RECV_ERROR;
			snprintf(
				stream->error_buffer,
				sizeof(stream->error_buffer),
				"curl_multi_poll: %s",
				curl_multi_strerror(multi_result)
			);
			return 0;
		}
	}
	return 1;
}

void* http_stream_open(
	const char* url,
	const char* method,
	const char* body
) {
	if (!url || !url[0])
		return NULL;
	ensure_curl_initialized();

	struct HttpStream* stream = calloc(1, sizeof(*stream));
	if (!stream)
		return NULL;
	stream->result = CURLE_OK;
	stream->easy = curl_easy_init();
	stream->multi = curl_multi_init();
	if (!stream->easy || !stream->multi)
		goto fail;

	curl_easy_setopt(stream->easy, CURLOPT_URL, url);
	curl_easy_setopt(stream->easy, CURLOPT_WRITEFUNCTION, stream_write_callback);
	curl_easy_setopt(stream->easy, CURLOPT_WRITEDATA, stream);
	curl_easy_setopt(stream->easy, CURLOPT_NOSIGNAL, 1L);
	curl_easy_setopt(stream->easy, CURLOPT_TCP_KEEPALIVE, 1L);
	curl_easy_setopt(stream->easy, CURLOPT_FOLLOWLOCATION, 1L);
	curl_easy_setopt(stream->easy, CURLOPT_MAXREDIRS, 10L);
	curl_easy_setopt(stream->easy, CURLOPT_CONNECTTIMEOUT, HTTP_CONNECT_TIMEOUT_SECONDS);
	curl_easy_setopt(stream->easy, CURLOPT_TIMEOUT, HTTP_REQUEST_TIMEOUT_SECONDS);
	curl_easy_setopt(stream->easy, CURLOPT_ERRORBUFFER, stream->error_buffer);

	if (method && strcmp(method, "POST") == 0) {
		curl_easy_setopt(stream->easy, CURLOPT_POST, 1L);
		curl_easy_setopt(
			stream->easy,
			CURLOPT_POSTFIELDSIZE,
			body ? (long)strlen(body) : 0L
		);
		curl_easy_setopt(stream->easy, CURLOPT_COPYPOSTFIELDS, body ? body : "");
	}

	if (curl_multi_add_handle(stream->multi, stream->easy) != CURLM_OK)
		goto fail;
	return stream;

fail:
	if (stream->multi && stream->easy)
		curl_multi_remove_handle(stream->multi, stream->easy);
	if (stream->easy)
		curl_easy_cleanup(stream->easy);
	if (stream->multi)
		curl_multi_cleanup(stream->multi);
	free(stream);
	return NULL;
}

int64_t http_stream_read(void* raw_stream, void* destination, size_t capacity) {
	struct HttpStream* stream = raw_stream;
	if (!stream || (!destination && capacity > 0))
		return -1;
	if (capacity == 0)
		return 0;

	stream->pending_limit = capacity;
	if (stream->paused && stream->pending_offset == stream->pending_size) {
		stream->paused = 0;
		CURLcode resume_result = curl_easy_pause(stream->easy, CURLPAUSE_CONT);
		if (resume_result != CURLE_OK) {
			stream->transfer_done = 1;
			stream->result = resume_result;
			return -1;
		}
	}
	http_stream_pump(stream);
	size_t available = stream->pending_size - stream->pending_offset;
	if (available > 0) {
		size_t count = available < capacity ? available : capacity;
		memcpy(destination, stream->pending + stream->pending_offset, count);
		stream->pending_offset += count;
		if (stream->pending_offset == stream->pending_size) {
			stream->pending_offset = 0;
			stream->pending_size = 0;
		}
		return (int64_t)count;
	}
	if (stream->transfer_done && stream->result != CURLE_OK)
		return -1;
	return 0;
}

int http_stream_done(void* raw_stream) {
	struct HttpStream* stream = raw_stream;
	if (!stream)
		return 1;
	return stream->transfer_done &&
		stream->pending_offset == stream->pending_size;
}

long http_stream_status(void* raw_stream) {
	struct HttpStream* stream = raw_stream;
	if (!stream)
		return 0;
	if (stream->status == 0 && stream->easy)
		curl_easy_getinfo(stream->easy, CURLINFO_RESPONSE_CODE, &stream->status);
	return stream->status;
}

const char* http_stream_error(void* raw_stream) {
	struct HttpStream* stream = raw_stream;
	if (!stream)
		return "invalid HTTP stream";
	if (!stream->transfer_done || stream->result == CURLE_OK)
		return NULL;
	if (stream->error_buffer[0])
		return stream->error_buffer;
	return curl_easy_strerror(stream->result);
}

void http_stream_close(void* raw_stream) {
	struct HttpStream* stream = raw_stream;
	if (!stream)
		return;
	if (stream->multi && stream->easy)
		curl_multi_remove_handle(stream->multi, stream->easy);
	if (stream->easy)
		curl_easy_cleanup(stream->easy);
	if (stream->multi)
		curl_multi_cleanup(stream->multi);
	free(stream->pending);
	free(stream);
}

static char* http_request(
	const char* url,
	const char* method,
	const char* body,
	const char** headers,
	size_t header_count
) {
	if (!url || !url[0])
		return NULL;

	struct Buffer buf = {
		malloc(1),
		0
	};

	if (!buf.data) {
		return NULL;
	}

	buf.data[0] = '\0';

	struct curl_slist* header_list = NULL;

	for (size_t i = 0; i < header_count; i++) {
		if (!headers[i])
			continue;

		struct curl_slist* new_list =
			curl_slist_append(header_list, headers[i]);

		if (!new_list) {
			curl_slist_free_all(header_list);
			free(buf.data);
			return NULL;
		}

		header_list = new_list;
	}

	char error_buffer[CURL_ERROR_SIZE] = {0};
	const int is_get = strcmp(method, "GET") == 0;
	const int attempts = is_get ? HTTP_GET_MAX_ATTEMPTS : 1;
	CURLcode res = CURLE_OK;

	for (int attempt = 0; attempt < attempts; ++attempt) {
		res = perform_request_attempt(
			url,
			method,
			body,
			header_list,
			&buf,
			error_buffer,
			attempt
		);

		if (res == CURLE_OK)
			break;

		if (!is_transient_error(res) || attempt + 1 == attempts)
			break;

		if (!reset_buffer(&buf)) {
			res = CURLE_OUT_OF_MEMORY;
			break;
		}

		retry_delay(attempt);
	}

	curl_slist_free_all(header_list);

	if (res != CURLE_OK) {
		fprintf(
			stderr,
			"http_request failed: %s\n",
			error_buffer[0]
				? error_buffer
				: curl_easy_strerror(res)
		);
		free(buf.data);
		return NULL;
	}

	return buf.data;
}

char* http_get(const char* url) {
	return http_request(
		url,
		"GET",
		NULL,
		NULL,
		0
	);
}

char* http_get_headers(
	const char* url,
	const char** headers,
	size_t header_count
) {
	return http_request(
		url,
		"GET",
		NULL,
		headers,
		header_count
	);
}

char* http_post(
	const char* url,
	const char* body
) {
	const char* headers[] = {
		"Content-Type: application/json"
	};

	return http_request(
		url,
		"POST",
		body,
		headers,
		1
	);
}

char* http_post_h(
	const char* url,
	const char* body,
	const char** headers,
	size_t header_count
) {
	return http_request(
		url,
		"POST",
		body,
		headers,
		header_count
	);
}

void http_free(char* data) {
	free(data);
}
