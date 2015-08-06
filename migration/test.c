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
static int64_t test_result(int64_t time_delta);
static int64_t start_time;
static bool migration_capability_compress;
static bool zero_iteration_done = false;
static uint64_t downtime;
static uint64_t transfered_bytes = 0;
static uint64_t initial_bytes = 0;
static uint64_t dirtied_bytes;
/*
static int qemu_test_get_buffer(void *opaque, uint8_t *buf,
                                int64_t pos, int size)
{
    return -1;
}
*/
static int qemu_test_put_buffer(void *opaque, const uint8_t *buf,
                                int64_t pos, int size)
{
    trace_qemu_test_put_buffer(size);
    transfered_bytes += size;
    return size;
}

static int qemu_test_close(void *opaque)
{
    MigrationState *s = opaque;
    qmp_migrate_set_downtime((double)(downtime)/1e9, NULL);
    s->enabled_capabilities[MIGRATION_CAPABILITY_COMPRESS] = migration_capability_compress;
    return 0;
}
/*
static int qemu_test_before_iterate(QEMUFile *f, void *opaque,
                                        uint64_t flags, void *data)
{
    return 0;
}
*/
static int64_t test_result(int64_t time_delta)
{
    double mbps = 1000;
    int64_t estimated_time_ms = 0;
    int64_t dt_ms;
    double Bpms = mbps * (1024 * 128 / 1000);
    double dirty_bytes_rate = dirtied_bytes/time_delta;
    uint64_t remaining = initial_bytes;
    uint64_t max_size = dirty_bytes_rate * (downtime / 1000000);
    if(dirty_bytes_rate < Bpms){
        do{
            dt_ms = remaining / Bpms;
            remaining = dt_ms * dirty_bytes_rate;
            estimated_time_ms += dt_ms;
        }while (remaining > max_size);
        estimated_time_ms += remaining / Bpms;
//TODO add working set estimation and comparing it with max_size
    } else {
        estimated_time_ms = -1;
    }
    trace_test_result ( initial_bytes, dirtied_bytes, dirty_bytes_rate, estimated_time_ms );
    return estimated_time_ms;
}
static int qemu_test_sync_hook(QEMUFile *f, void *opaque,
                                        uint64_t flags, void *data)
{
    int64_t end_time, time_delta;
// if we got all information we should make our estimation and stop process
    if (zero_iteration_done) {
        end_time = qemu_clock_get_ns(QEMU_CLOCK_REALTIME);
        if ( end_time - start_time < downtime ){
            usleep( (downtime - (end_time - start_time))/ 1000);
            time_delta = downtime / 1000000;
        } else {
            time_delta = (end_time - start_time) / 1000000;
        }
        dirtied_bytes = transfered_bytes - initial_bytes;
        test_result(time_delta);
        return -1;
    } else {
        zero_iteration_done = true;
        initial_bytes = transfered_bytes;
    }
        return 0;
}

static int qemu_test_after_iterate(QEMUFile *f, void *opaque,
                                        uint64_t flags, void *data)
{
    switch ( flags ){
    case RAM_CONTROL_SETUP:
        start_time = qemu_clock_get_ns(QEMU_CLOCK_REALTIME);
        break;
    case RAM_CONTROL_ROUND:
        break;
//WE SHOULD NEVER GET HERE!
    case RAM_CONTROL_FINISH:
        trace_probe_timestamp();
    }
    return 0;
}

static size_t qemu_test_save_page(QEMUFile *f, void *opaque,
                                  ram_addr_t block_offset, ram_addr_t offset,
                                  size_t size, uint64_t *bytes_sent)
{
    trace_qemu_test_save_page(size);
    transfered_bytes += size;
    *bytes_sent = size;
    return size;
}

//do not use for incoming
/*
static const QEMUFileOps test_read_ops = {
    .get_buffer         = qemu_test_get_buffer,
    .close              = qemu_test_close,
};
*/
static const QEMUFileOps test_write_ops = {
    .hook_ram_sync      = qemu_test_sync_hook,
    .put_buffer         = qemu_test_put_buffer,
    .close              = qemu_test_close,
    .save_page          = qemu_test_save_page,
    .after_ram_iterate  = qemu_test_after_iterate,
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
    downtime = migrate_max_downtime();
    qmp_migrate_set_downtime(0, NULL);
    s->file = qemu_fopen_test(s, "wb");
    migration_capability_compress = s->enabled_capabilities[MIGRATION_CAPABILITY_COMPRESS];
    s->enabled_capabilities[MIGRATION_CAPABILITY_COMPRESS] = false;
    migrate_fd_connect(s);
    return;
}

