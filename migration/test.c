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
static int64_t test_result(void);

static uint64_t downtime;
static uint64_t transfered_bytes = 0;
static int64_t rounds = -1;
static int64_t times = 0;
static uint64_t initial_bytes = 0;
static uint64_t dirtied_bytes;
static int qemu_test_get_buffer(void *opaque, uint8_t *buf,
                                int64_t pos, int size)
{
    return -1;
}

static int qemu_test_put_buffer(void *opaque, const uint8_t *buf,
                                int64_t pos, int size)
{
    trace_qemu_test_put_buffer(size);
    transfered_bytes += size;
    return size;
}

static int qemu_test_close(void *opaque)
{
    qmp_migrate_set_downtime((double)(downtime)/1e9, NULL);
    return 0;
}

static int qemu_test_before_iterate(QEMUFile *f, void *opaque,
                                        uint64_t flags, void *data)
{
    return 0;
}

static int64_t test_result(void)
{
    double mbps = 1000;
    int64_t time_delta = downtime/1000000;
    int64_t estimated_time_ms = 0;
    int64_t dt_ms;
    double Bpms = mbps * (1024 * 128 / 1000);
    double dirty_bytes_rate = dirtied_bytes/time_delta;
    uint64_t remaining = initial_bytes;
    uint64_t max_size = dirty_bytes_rate * (downtime / 1000000);
    do{
        dt_ms = remaining / Bpms;
        remaining = dt_ms * dirty_bytes_rate;
        estimated_time_ms += dt_ms;
    }while (remaining > max_size);
    dt_ms = remaining / (mbps * 8) * 1000;
    estimated_time_ms += dt_ms;
    trace_test_result ( initial_bytes, dirtied_bytes, dirty_bytes_rate, estimated_time_ms );
    return estimated_time_ms;
}


static int qemu_test_after_iterate(QEMUFile *f, void *opaque,
                                        uint64_t flags, void *data)
{
    MigrationState *s = opaque;
    switch ( flags ){
    case RAM_CONTROL_SETUP:
        times++;
        trace_probe_timestamp();
        break;
    case RAM_CONTROL_ROUND:
        if ( rounds == -1 ){
            rounds = times * 2;
        }
        rounds--;
        if ( (rounds % times) == 0 ){
            if ( initial_bytes == 0 ){
                initial_bytes = transfered_bytes;
                transfered_bytes = 0;
                usleep( downtime / 1000 );
            } else {
                dirtied_bytes = transfered_bytes;
                test_result();
            }
        }
        if ( rounds == 0 ) {
            //push result UP
           // migrate_set_state_wrap(s, MIGRATION_STATUS_ACTIVE,
           //                           MIGRATION_STATUS_CANCELLING);
            qemu_file_set_error (s->file, 1);
            transfered_bytes = 0;
            rounds = -1;
            times = 0;
            initial_bytes = 0;
        }
        break;
//WE SHOULD NEVER GET HERE!
    case RAM_CONTROL_FINISH:
        trace_probe_timestamp();
        migrate_set_state_wrap(s, MIGRATION_STATUS_ACTIVE,
                                      MIGRATION_STATUS_FAILED);
    }

    return 0;
}
/*
static size_t qemu_test_save_page(QEMUFile *f, void *opaque,
                                  ram_addr_t block_offset, ram_addr_t offset,
                                  size_t size, uint64_t *bytes_sent)
{
    trace_qemu_test_save_page(size);
    transfered_bytes += size;
    *bytes_sent = size;
    return size;
}
*/
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
//    .save_page          = qemu_test_save_page,
};

static void *qemu_fopen_test(MigrationState *s, const char *mode)
{
    QEMUFileTest *t;
    downtime = migrate_max_downtime();
    qmp_migrate_set_downtime(0, NULL);
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
//TODO measure bandwidth
void test_start_migration(void *opaque, const char *host_port, Error **errp)
{
    MigrationState *s = opaque;
    s->file = qemu_fopen_test(s, "wb");
    migrate_fd_connect(s);
    return;
}

