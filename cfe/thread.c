#include <stdint.h>
#include <stdlib.h>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <pthread.h>
#endif

typedef void* (*ferra_thread_callback)(void*);

struct FerraThread {
#if defined(_WIN32)
	HANDLE handle;
#else
	pthread_t handle;
#endif
};

#if defined(_WIN32)
struct FerraThreadStart {
	ferra_thread_callback callback;
	void* context;
};

static DWORD WINAPI ferra_thread_trampoline(void* raw_start) {
	struct FerraThreadStart* start = (struct FerraThreadStart*)raw_start;
	ferra_thread_callback callback = start->callback;
	void* context = start->context;
	free(start);
	callback(context);
	return 0;
}
#endif

int32_t ferra_thread_create(
	void** out_handle,
	void* raw_callback,
	void* context
) {
	if (out_handle == NULL || raw_callback == NULL) return -1;
	*out_handle = NULL;

	struct FerraThread* thread = malloc(sizeof(*thread));
	if (thread == NULL) return -1;

#if defined(_WIN32)
	struct FerraThreadStart* start = malloc(sizeof(*start));
	if (start == NULL) {
		free(thread);
		return -1;
	}
	start->callback = (ferra_thread_callback)raw_callback;
	start->context = context;
	thread->handle = CreateThread(
		NULL, 0, ferra_thread_trampoline, start, 0, NULL);
	if (thread->handle == NULL) {
		free(start);
		free(thread);
		return (int32_t)GetLastError();
	}
#else
	int result = pthread_create(
		&thread->handle,
		NULL,
		(ferra_thread_callback)raw_callback,
		context
	);
	if (result != 0) {
		free(thread);
		return (int32_t)result;
	}
#endif

	*out_handle = thread;
	return 0;
}

int32_t ferra_thread_join(void* raw_handle) {
	if (raw_handle == NULL) return -1;
	struct FerraThread* thread = (struct FerraThread*)raw_handle;

#if defined(_WIN32)
	DWORD wait_result = WaitForSingleObject(thread->handle, INFINITE);
	if (wait_result != WAIT_OBJECT_0) return (int32_t)GetLastError();
	CloseHandle(thread->handle);
#else
	int result = pthread_join(thread->handle, NULL);
	if (result != 0) return (int32_t)result;
#endif

	free(thread);
	return 0;
}

void ferra_thread_exit(void) {
#if defined(_WIN32)
	ExitThread(0);
#else
	pthread_exit(NULL);
#endif
}

void *ferra_mutex_create(void) {
	#if defined(_WIN32)
	CRITICAL_SECTION *mutex = malloc(sizeof(*mutex));
	if (mutex == NULL) return NULL;
	InitializeCriticalSection(mutex);
	return mutex;
	#else
	pthread_mutex_t *mutex = malloc(sizeof(*mutex));
	if (mutex == NULL) return NULL;

	if (pthread_mutex_init(mutex, NULL) != 0) {
		free(mutex);
		return NULL;
	}
	return mutex;
	#endif
}

int32_t ferra_mutex_lock(void *handle) {
	if (handle == NULL) return -1;
	#if defined(_WIN32)
	EnterCriticalSection((CRITICAL_SECTION *)handle);
	return 0;
	#else
	return (int32_t)pthread_mutex_lock((pthread_mutex_t *)handle);
	#endif
}

int32_t ferra_mutex_unlock(void *handle) {
	if (handle == NULL) return -1;
	#if defined(_WIN32)
	LeaveCriticalSection((CRITICAL_SECTION *)handle);
	return 0;
	#else
	return (int32_t)pthread_mutex_unlock((pthread_mutex_t *)handle);
	#endif
}

int32_t ferra_mutex_destroy(void *handle) {
	if (handle == NULL) return 0;

	#if defined(_WIN32)
	DeleteCriticalSection((CRITICAL_SECTION *)handle);
	free(handle);
	return 0;
	#else
	int result = pthread_mutex_destroy((pthread_mutex_t *)handle);
	if (result == 0) free(handle);
	return (int32_t)result;
	#endif
}
