#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <errno.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

static size_t ferra_argument_count(char* const* args) {
	size_t count = 0;
	if (args == NULL) return 0;
	while (args[count] != NULL) ++count;
	return count;
}

#if defined(_WIN32)
static size_t windows_quoted_size(const char* value) {
	size_t size = 2;
	size_t slashes = 0;
	for (const char* current = value; *current; ++current) {
		if (*current == '\\') {
			++slashes;
		} else if (*current == '"') {
			size += slashes * 2 + 2;
			slashes = 0;
		} else {
			size += slashes + 1;
			slashes = 0;
		}
	}
	return size + slashes * 2 + 1;
}

static char* windows_append_quoted(char* out, const char* value) {
	*out++ = '"';
	size_t slashes = 0;
	for (const char* current = value; *current; ++current) {
		if (*current == '\\') {
			++slashes;
			continue;
		}
		if (*current == '"') {
			while (slashes > 0) {
				*out++ = '\\';
				*out++ = '\\';
				--slashes;
			}
			*out++ = '\\';
			*out++ = '"';
			slashes = 0;
			continue;
		}
		while (slashes > 0) {
			*out++ = '\\';
			--slashes;
		}
		slashes = 0;
		*out++ = *current;
	}
	while (slashes > 0) {
		*out++ = '\\';
		*out++ = '\\';
		--slashes;
	}
	*out++ = '"';
	return out;
}

static char* windows_command_line(
	const char* executable,
	char* const* args
) {
	size_t size = windows_quoted_size(executable) + 1;
	for (size_t i = 0; args != NULL && args[i] != NULL; ++i) {
		size += windows_quoted_size(args[i]) + 1;
	}
	char* command = malloc(size);
	if (command == NULL) return NULL;

	char* out = windows_append_quoted(command, executable);
	for (size_t i = 0; args != NULL && args[i] != NULL; ++i) {
		*out++ = ' ';
		out = windows_append_quoted(out, args[i]);
	}
	*out = '\0';
	return command;
}

static int windows_start_process(
	const char* executable,
	char* const* args,
	HANDLE output,
	PROCESS_INFORMATION* process
) {
	char* command = windows_command_line(executable, args);
	if (command == NULL) return 0;

	STARTUPINFOA startup;
	ZeroMemory(&startup, sizeof(startup));
	startup.cb = sizeof(startup);
	if (output != NULL) {
		startup.dwFlags = STARTF_USESTDHANDLES;
		startup.hStdOutput = output;
		startup.hStdError = GetStdHandle(STD_ERROR_HANDLE);
		startup.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
	}
	ZeroMemory(process, sizeof(*process));
	BOOL started = CreateProcessA(
		executable,
		command,
		NULL,
		NULL,
		output != NULL,
		0,
		NULL,
		NULL,
		&startup,
		process
	);
	free(command);
	return started != 0;
}
#else
static char** posix_arguments(const char* executable, char* const* args) {
	const size_t count = ferra_argument_count(args);
	char** result = malloc(sizeof(*result) * (count + 2));
	if (result == NULL) return NULL;
	result[0] = (char*)executable;
	for (size_t i = 0; i < count; ++i) result[i + 1] = args[i];
	result[count + 1] = NULL;
	return result;
}
#endif

int32_t ferra_process_run(const char* executable, char* const* args) {
	if (executable == NULL || executable[0] == '\0') return -1;

#if defined(_WIN32)
	PROCESS_INFORMATION process;
	if (!windows_start_process(executable, args, NULL, &process)) {
		return -(int32_t)GetLastError();
	}
	WaitForSingleObject(process.hProcess, INFINITE);
	CloseHandle(process.hThread);
	CloseHandle(process.hProcess);
	return 0;
#else
	char** argv = posix_arguments(executable, args);
	if (argv == NULL) return -1;
	pid_t pid = fork();
	if (pid < 0) {
		free(argv);
		return -1;
	}
	if (pid == 0) {
		execv(executable, argv);
		_exit(127);
	}
	free(argv);
	while (waitpid(pid, NULL, 0) < 0 && errno == EINTR) {
	}
	return 0;
#endif
}

int64_t ferra_process_output(
	const char* executable,
	char* const* args,
	void* raw_output,
	size_t capacity
) {
	if (executable == NULL || executable[0] == '\0' ||
		raw_output == NULL || capacity == 0) {
		return -1;
	}
	char* output = (char*)raw_output;
	size_t used = 0;
	output[0] = '\0';

#if defined(_WIN32)
	SECURITY_ATTRIBUTES security = {
		sizeof(SECURITY_ATTRIBUTES), NULL, TRUE
	};
	HANDLE read_pipe = NULL;
	HANDLE write_pipe = NULL;
	if (!CreatePipe(&read_pipe, &write_pipe, &security, 0)) return -1;
	SetHandleInformation(read_pipe, HANDLE_FLAG_INHERIT, 0);

	PROCESS_INFORMATION process;
	if (!windows_start_process(executable, args, write_pipe, &process)) {
		CloseHandle(read_pipe);
		CloseHandle(write_pipe);
		return -1;
	}
	CloseHandle(write_pipe);

	char chunk[4096];
	DWORD count = 0;
	while (ReadFile(read_pipe, chunk, sizeof(chunk), &count, NULL) && count > 0) {
		size_t available = capacity - 1 - used;
		size_t copy = count < available ? count : available;
		if (copy > 0) {
			memcpy(output + used, chunk, copy);
			used += copy;
		}
	}
	WaitForSingleObject(process.hProcess, INFINITE);
	CloseHandle(process.hThread);
	CloseHandle(process.hProcess);
	CloseHandle(read_pipe);
#else
	int pipes[2];
	if (pipe(pipes) != 0) return -1;
	char** argv = posix_arguments(executable, args);
	if (argv == NULL) {
		close(pipes[0]);
		close(pipes[1]);
		return -1;
	}

	pid_t pid = fork();
	if (pid < 0) {
		free(argv);
		close(pipes[0]);
		close(pipes[1]);
		return -1;
	}
	if (pid == 0) {
		close(pipes[0]);
		dup2(pipes[1], STDOUT_FILENO);
		close(pipes[1]);
		execv(executable, argv);
		_exit(127);
	}
	free(argv);
	close(pipes[1]);

	char chunk[4096];
	ssize_t count;
	while ((count = read(pipes[0], chunk, sizeof(chunk))) > 0) {
		size_t available = capacity - 1 - used;
		size_t copy = (size_t)count < available ? (size_t)count : available;
		if (copy > 0) {
			memcpy(output + used, chunk, copy);
			used += copy;
		}
	}
	close(pipes[0]);
	while (waitpid(pid, NULL, 0) < 0 && errno == EINTR) {
	}
#endif

	output[used] = '\0';
	return (int64_t)used;
}
