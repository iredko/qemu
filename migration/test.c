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
#include "qmp-commands.h"

typedef struct QEMUFileTest {
    MigrationState *s;
    size_t len;
    QEMUFile *file;
} QEMUFileTest;

//stubs
static bool zero_iteration_done = false;
static uint64_t transfered_bytes = 0;
static uint64_t initial_bytes = 0;

static int qemu_test_put_buffer(void *opaque, const uint8_t *buf,
                                int64_t pos, int size)
{
    trace_qemu_test_put_buffer(size);
    transfered_bytes += size;
    return size;
}

static int qemu_test_close(void *opaque)
{
    return 0;
}

static int qemu_test_sync_hook(QEMUFile *f, void *opaque,
                                        uint64_t flags, void *data)
{
    static uint64_t dirtied_bytes;
    uint64_t downtime = 0;
    int64_t time_delta;
    uint64_t remaining_bytes = *((uint64_t*) data);
    MigrationState *s = (MigrationState*) opaque;
    /* First call will be from ram_save_begin
     * so we need to save initial size of VM memory
     * and sleep for decent period (downtime for example). */
    if (!zero_iteration_done) {
        downtime = migrate_max_downtime();
        zero_iteration_done = true;
        initial_bytes = remaining_bytes;
        usleep( downtime / 1000);
    } else {
    /* Second and last call will be from ram_save_iterate.
     * We assume that time between two synchronizations of
     * dirty bitmap differs from downtime negligibly and
     * make our estimation of dirty bytes rate. */
        dirtied_bytes = remaining_bytes;
        time_delta = downtime / 1000000;
        s->dirty_bytes_rate = dirtied_bytes * 1000 / time_delta;
        return -42;
    }
        return 0;
}

static const QEMUFileOps test_write_ops = {
    .hook_ram_sync      = qemu_test_sync_hook,
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

//TODO host_port param should become params (bandwidth, etc.)
//TODO measure bandwidth
void test_start_migration(void *opaque, const char *host_port, Error **errp)
{
    MigrationState *s = opaque;
    s->file = qemu_fopen_test(s, "wb");
    migrate_fd_connect(s);
    return;
}

