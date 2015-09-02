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
#include "qmp-commands.h"

typedef struct QEMUFileTest {
    MigrationState *s;
    size_t len;
    QEMUFile *file;
} QEMUFileTest;

//stubs
static bool zero_iteration_done = false;
static uint64_t downtime;
static uint64_t transfered_bytes = 0;
static uint64_t initial_bytes = 0;

static int qemu_test_put_buffer(void *opaque, const uint8_t *buf,
                                int64_t pos, int size)
{
    transfered_bytes += size;
    return size;
}

static int qemu_test_close(void *opaque)
{
    qmp_migrate_set_downtime((double)(downtime)/1e9, NULL);
    return 0;
}

static const QEMUFileOps test_write_ops = {
    .put_buffer         = qemu_test_put_buffer,
    .close              = qemu_test_close,
};

static void *qemu_fopen_test(MigrationState *s, const char *mode)
{
    QEMUFileTest *t;
    transfered_bytes = 0;
    initial_bytes = 0;
    zero_iteration_done = false;
    if (qemu_file_mode_is_not_valid(mode)) {
        return NULL;
    }

    t = g_malloc0(sizeof(QEMUFileTest));
    t->s = s;

    if (mode[0] == 'w') {
        t->file = qemu_fopen_ops(s, &test_write_ops);
    } else {
        return NULL;
    }
    qemu_file_set_rate_limit(t->file, -1);
    return t->file;
}

void test_start_migration(void *opaque, const char *host_port, Error **errp)
{
    MigrationState *s = opaque;    
    s->file = qemu_fopen_test(s, "wb");
    /* This workaround in case if we want to avoid
       additional checks of capability "test-only"
       in migration code.
       At the end of measurement real downtime will
       be unstashed */
    downtime = migrate_max_downtime();
    qmp_migrate_set_downtime(0, NULL);
    migrate_fd_connect(s);
    return;
}

