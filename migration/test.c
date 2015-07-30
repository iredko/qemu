#include "qemu-common.h"
#include "migration/migration.h"
#include "migration/qemu-file.h"
#include "exec/cpu-common.h"
#include "qemu/error-report.h"
#include "qemu/main-loop.h"
#include "qemu/sockets.h"
#include "qemu/bitmap.h"
#include <stdio.h>
#include <string.h>
#include "trace.h"

typedef struct QEMUFileTest {
    MigrationState *s;
    size_t len;
    QEMUFile *file;
} QEMUFileTest;

//stubs
static int qemu_test_get_buffer(void *opaque, uint8_t *buf,
                                int64_t pos, int size)
{
    return -1;
}

static int qemu_test_put_buffer(void *opaque, const uint8_t *buf,
                                int64_t pos, int size)
{
    return size;
}

static int qemu_test_close(void *opaque)
{
    return 0;
}

static int qemu_test_before_iterate(QEMUFile *f, void *opaque,
                                        uint64_t flags, void *data)
{
    return 0;
}

static int qemu_test_after_iterate(QEMUFile *f, void *opaque,
                                        uint64_t flags, void *data)
{
    return 0;
}

static size_t qemu_test_save_page(QEMUFile *f, void *opaque,
                                  ram_addr_t block_offset, ram_addr_t offset,
                                  size_t size, uint64_t *bytes_sent)
{
    return size;
}

//do not use for incoming
static const QEMUFileOps test_read_ops = {
    .get_buffer         = qemu_test_get_buffer,
    .close              = qemu_test_close,
};

static const QEMUFileOps test_write_ops = {
    .put_buffer         = qemu_test_put_buffer,
    .close              = qemu_test_close,
    .before_ram_iterate = qemu_test_before_iterate,
    .after_ram_iterate  = qemu_test_after_iterate,
    .save_page          = qemu_test_save_page,
};

static void *qemu_fopen_test(MigrationState *s, const char *mode)
{
    QEMUFileTest *t;

    if (qemu_file_mode_is_not_valid(mode)) {
        return NULL;
    }

    t = g_malloc0(sizeof(QEMUFileTest));
    t->s = s;

    if (mode[0] == 'w') {
        t->file = qemu_fopen_ops(s, &test_write_ops);
    } else {
        t->file = qemu_fopen_ops(s, &test_read_ops);
    }

    return t->file;
}

//TODO host_port param should become params (bandwidth, etc.)
void test_start_migration(void *opaque, const char *host_port, Error **errp)
{
    MigrationState *s = opaque;
    
    s->file = qemu_fopen_test(s, "wb");
    
//TODO change to migrate_fd_connect eventualy, to see what happen
    migrate_test_connect(s);
    return;
}

