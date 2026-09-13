/*
 * Verifies direct AES shell invocation context, environment lookup, path
 * search and atomic access to the one global shell buffer.
 *
 * MIT License (see: LICENSE)
 * Copyright (C) 2026 tomaz stih
 */

#include <gem.h>

#include <assert.h>
#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>
#include <threads.h>

enum { TEST_BUFFER_SIZE = 4096, TEST_ITERATIONS = 200 };

typedef struct writer_context {
    char value;
    atomic_int *failed;
} writer_context_t;

static int buffer_is_complete(const char *buffer)
{
    size_t index;
    char value = buffer[0];

    if (value != 'A' && value != 'B') {
        return 0;
    }
    for (index = 1u; index < TEST_BUFFER_SIZE; ++index) {
        if (buffer[index] != value) {
            return 0;
        }
    }
    return 1;
}

static int write_shell_buffer(void *argument)
{
    writer_context_t *context = (writer_context_t *)argument;
    char source[TEST_BUFFER_SIZE];
    char result[TEST_BUFFER_SIZE];
    int iteration;

    memset(source, context->value, sizeof(source));
    for (iteration = 0; iteration < TEST_ITERATIONS; ++iteration) {
        if (shel_put(source, sizeof(source)) == 0 ||
            shel_get(result, sizeof(result)) == 0 ||
            buffer_is_complete(result) == 0) {
            atomic_store(context->failed, 1);
            return 1;
        }
    }
    return 0;
}

int main(int argc, char **argv)
{
    char command[256];
    char tail[128] = {0};
    char path[260] = "clock.app";
    char final[TEST_BUFFER_SIZE];
    char *environment = NULL;
    atomic_int failed = 0;
    writer_context_t first = {'A', &failed};
    writer_context_t second = {'B', &failed};
    thrd_t first_thread;
    thrd_t second_thread;
    int first_result;
    int second_result;

    assert(argc == 2 && strcmp(argv[1], "notes.txt") == 0);
    assert(shel_read(command, tail));
    assert(strstr(command, "test_aes_shell") != NULL);
    assert((unsigned char)tail[0] == strlen("notes.txt"));
    assert(memcmp(tail + 1, "notes.txt", strlen("notes.txt")) == 0);
    assert(shel_envrn(&environment, "SHELL_TEST_VALUE="));
    assert(strcmp(environment, "works") == 0);
    assert(shel_find(path));
    assert(path[0] == '/');
    strcpy(path, argv[0]);
    assert(shel_find(path));
    assert(path[0] == '/');

    memset(final, 'A', sizeof(final));
    assert(shel_put(final, sizeof(final)));
    assert(shel_get(NULL, SHEL_BUFSIZE) == TEST_BUFFER_SIZE);
    assert(thrd_create(&first_thread, write_shell_buffer, &first) ==
           thrd_success);
    assert(thrd_create(&second_thread, write_shell_buffer, &second) ==
           thrd_success);
    assert(thrd_join(first_thread, &first_result) == thrd_success);
    assert(thrd_join(second_thread, &second_result) == thrd_success);
    assert(first_result == 0 && second_result == 0);
    assert(atomic_load(&failed) == 0);
    assert(shel_get(final, sizeof(final)));
    assert(buffer_is_complete(final));
    assert(shel_get(NULL, SHEL_BUFSIZE) == TEST_BUFFER_SIZE);
    return 0;
}
