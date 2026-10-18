/* SPDX-FileCopyrightText: 2026 Barry Carter <barry.carter@gmail.com> */
/* SPDX-License-Identifier: Apache-2.0 */
#include "discovery.c"
#include "fixture.h"

static GAPLEConnection connection = {{1}};
static bool valid = true, mapped = true;
static unsigned lock_depth, queue_done, completions, changed, lease_calls;
static enum pbl_bt_errno completed_error;
static unsigned live, emitted;
static size_t live_bytes;
static struct pbl_bt_gatt_service *services[32];
static int (*queued_start)(void *);
static void *queued_arg;
typedef enum {
  NONE,
  SVC,
  CHR,
  DSC
} Kind;
static struct {
  Kind kind;
  uint16_t start, end;
  SvcFn svc;
  ChrFn chr;
  DscFn dsc;
  void *arg;
} pending;
static Kind fail_kind;
static int fail_status = 17, write_status;
static WriteFn write_cb;
static void *write_arg;
static unsigned writes;

void *kernel_zalloc_check(size_t size) {
  size_t *p = calloc(1, size + sizeof(size_t));
  assert(p);
  *p = size;
  ++live;
  live_bytes += size;
  return p + 1;
}
void kernel_free(void *data) {
  if (!data)
    return;
  size_t *p = (size_t *)data - 1;
  assert(live && live_bytes >= *p);
  --live;
  live_bytes -= *p;
  free(p);
}
void bt_lock(void) {
  ++lock_depth;
}
void bt_unlock(void) {
  assert(lock_depth);
  --lock_depth;
}
bool gap_le_connection_is_valid(const GAPLEConnection *c) {
  assert(lock_depth && c == &connection);
  return valid;
}
bool pebble_device_to_nimble_conn_handle(const struct pbl_bt_device_internal *d, uint16_t *h) {
  assert(d == &connection.device);
  *h = 42;
  return mapped;
}
int ble_uuid_cmp(const ble_uuid_t *a, const ble_uuid_t *b) {
  return a->value != b->value;
}
void ble_uuid_to_str(const ble_uuid_t *u, char *s) {
  (void)u;
  s[0] = 0;
}
void uuid_to_string(const Uuid *u, char *s) {
  (void)u;
  s[0] = 0;
}
void nimble_uuid_to_pebble(const ble_uuid_any_t *n, Uuid *u) {
  memset(u, 0, sizeof(*u));
  memcpy(u, &n->u.value, sizeof(n->u.value));
}
void conn_mgr_set_ble_conn_response_time(GAPLEConnection *c, unsigned consumer, unsigned response,
                                         unsigned seconds) {
  assert(c == &connection && consumer == 1 && response == 2);
  assert(seconds == CONFIG_BT_GATT_DISCOVERY_LEASE_SECONDS);
  ++lease_calls;
}
void nimble_gattc_op_queue_push(int (*fn)(void *), void *arg) {
  assert(!queued_start);
  queued_start = fn;
  queued_arg = arg;
}
void nimble_gattc_op_queue_complete(void) {
  assert(queued_start && !queue_done);
  ++queue_done;
  kernel_free(queued_arg);
  queued_arg = NULL;
  queued_start = NULL;
}
static void run_queued(void) {
  int (*fn)(void *) = queued_start;
  if (fn(queued_arg)) {
    assert(!queue_done);
    kernel_free(queued_arg);
    queued_arg = NULL;
    queued_start = NULL;
  }
}
int ble_gattc_disc_all_svcs(uint16_t h, SvcFn cb, void *arg) {
  assert(h == 42 && pending.kind == NONE);
  if (fail_kind == SVC)
    return fail_status;
  pending.kind = SVC;
  pending.svc = cb;
  pending.arg = arg;
  return 0;
}
int ble_gattc_disc_all_chrs(uint16_t h, uint16_t start, uint16_t end, ChrFn cb, void *arg) {
  assert(h == 42 && pending.kind == NONE && start <= end);
  if (fail_kind == CHR)
    return fail_status;
  pending.kind = CHR;
  pending.start = start;
  pending.end = end;
  pending.chr = cb;
  pending.arg = arg;
  return 0;
}
int ble_gattc_disc_all_dscs(uint16_t h, uint16_t start, uint16_t end, DscFn cb, void *arg) {
  assert(h == 42 && pending.kind == NONE && start <= end);
  if (fail_kind == DSC)
    return fail_status;
  pending.kind = DSC;
  pending.start = start;
  pending.end = end;
  pending.dsc = cb;
  pending.arg = arg;
  return 0;
}
int ble_gattc_write_flat(uint16_t h, uint16_t dsc, const void *value, uint16_t size, WriteFn cb,
                         void *arg) {
  assert(h == 42 && dsc == 4 && size == 2 && *(const uint16_t *)value == 2);
  assert(!write_cb);
  ++writes;
  if (write_status)
    return write_status;
  write_cb = cb;
  write_arg = arg;
  return 0;
}
void pbl_bt_cb_gatt_client_discovery_handle_indication(GAPLEConnection *c,
                                                       struct pbl_bt_gatt_service *service,
                                                       enum pbl_bt_errno error) {
  assert(valid && lock_depth && c == &connection && !error && emitted < 32);
  assert(service->size_bytes ==
         PBL_BT_GATT_SERVICE_SIZE_BYTES(service->num_characteristics, service->num_descriptors, 0));
  services[emitted++] = service; // Retain the blob as the firmware does.
}
bool pbl_bt_cb_gatt_client_discovery_complete(GAPLEConnection *c, enum pbl_bt_errno error) {
  assert(valid && lock_depth && c == &connection && !s_discovery_in_progress);
  assert(!completions);
  ++completions;
  completed_error = error;
  return true;
}
void pbl_bt_cb_gatt_client_discovery_handle_service_changed(GAPLEConnection *c, uint16_t chr) {
  assert(valid && lock_depth && c == &connection && chr == 3);
  ++changed;
}
static int terminal(int status) {
  const struct ble_gatt_error error = {status};
  Kind kind = pending.kind;
  void *arg = pending.arg;
  pending.kind = NONE;
  if (kind == SVC)
    return pending.svc(42, &error, NULL, arg);
  if (kind == CHR)
    return pending.chr(42, &error, NULL, arg);
  assert(kind == DSC);
  return pending.dsc(42, &error, pending.start, NULL, arg);
}
int pbl_sem_give(struct pbl_sem *sem) {
  assert(!sem->count);
  ++sem->count;
  return 0;
}
int pbl_sem_take(struct pbl_sem *sem, int timeout) {
  assert(timeout == PBL_FOREVER);
  // Deliver the host callback while the caller waits for cancellation.
  assert(terminal(BLE_HS_EDONE) == BLE_HS_EDONE);
  assert(sem->count == 1);
  --sem->count;
  return 0;
}
static void service(uint16_t start, uint16_t end, uint16_t uuid) {
  assert(pending.kind == SVC);
  struct ble_gatt_svc svc = {start, end, {{uuid}}};
  struct ble_gatt_error e = {0};
  assert(!pending.svc(42, &e, &svc, pending.arg));
}
static void characteristic(uint16_t def, uint16_t value, uint16_t uuid) {
  assert(pending.kind == CHR);
  struct ble_gatt_chr chr = {def, value, 0x22, {{uuid}}};
  struct ble_gatt_error e = {0};
  assert(!pending.chr(42, &e, &chr, pending.arg));
}
static void descriptor(uint16_t handle, uint16_t uuid) {
  assert(pending.kind == DSC);
  struct ble_gatt_dsc dsc = {handle, {{uuid}}};
  struct ble_gatt_error e = {0};
  assert(!pending.dsc(42, &e, pending.start, &dsc, pending.arg));
}
static void start(void) {
  struct pbl_bt_att_handle_range range = {1, 1000};
  assert(pbl_bt_gatt_start_discovery_range(&connection, &range) == PBL_BT_ERRNO_OK);
  assert(lease_calls == (CONFIG_BT_GATT_DISCOVERY_LEASE_SECONDS > 0));
  run_queued();
}
static void one_service(bool subscription) {
  start();
  service(1, 10, subscription ? 0x1801 : 0x180f);
  terminal(BLE_HS_EDONE);
  characteristic(2, 3, subscription ? 0x2a05 : 0x2a19);
  terminal(BLE_HS_EDONE);
  descriptor(4, subscription ? 0x2902 : 0x2901);
}
static void release_services(void) {
  for (unsigned i = 0; i < emitted; ++i)
    kernel_free(services[i]);
  emitted = 0;
}
static void check_done(unsigned indications, unsigned callbacks) {
  assert(queue_done == 1 && completions == callbacks && emitted == indications);
  assert(!pending.kind && !s_discovery_in_progress && !lock_depth);
  release_services();
  assert(!live && !live_bytes);
}
int main(int argc, char **argv) {
  assert(argc == 2);
  const char *test = argv[1];
  if (!strcmp(test, "incremental")) {
    start();
    service(1, 10, 0x180f);
    service(20, 30, 0x1810);
    terminal(BLE_HS_EDONE);
    characteristic(2, 3, 0x2a19);
    characteristic(6, 7, 0x2a20);
    terminal(BLE_HS_EDONE);
    characteristic(21, 22, 0x2a21);
    terminal(BLE_HS_EDONE);
    assert(pending.start == 2 && pending.end == 5);
    descriptor(4, 0x2901);
    terminal(BLE_HS_EDONE);
    assert(pending.start == 6 && pending.end == 10);
    descriptor(8, 0x2902);
    unsigned before = live;
    terminal(BLE_HS_EDONE);
    assert(emitted == 1 && !completions && !queue_done);
    assert(live ==
           before - 4); // Two characteristics, two descriptors and service freed; one blob added.
    assert(list_count(((GATTServiceDiscoveryContext *)pending.arg)->services) == 1);
    struct pbl_bt_gatt_service *svc = services[0];
    assert(svc->att_handle == 1 && svc->num_characteristics == 2 && svc->num_descriptors == 2);
    struct pbl_bt_gatt_characteristic *chr = svc->characteristics;
    assert(chr->att_handle_offset == 2 && chr->properties == 0x22 && chr->num_descriptors == 1);
    assert(chr->descriptors[0].att_handle_offset == 3);
    chr = (void *)&chr->descriptors[1];
    assert(chr->att_handle_offset == 6 && chr->num_descriptors == 1);
    assert(chr->descriptors[0].att_handle_offset == 7);
    assert(pending.start == 21 && pending.end == 30);
    terminal(BLE_HS_EDONE);
    assert(!completed_error);
    check_done(2, 1);
  } else if (!strcmp(test, "large-database")) {
    // Exercise a larger database and incremental node release under both lease policies.
    enum {
      SERVICE_COUNT = 24,
      CHARS_PER_SERVICE = 4,
      DESCS_PER_CHAR = 3
    };
    start();
    for (unsigned i = 0; i < SERVICE_COUNT; ++i) {
      uint16_t base = 1 + 40 * i;
      service(base, base + 39, 0x180f);
    }
    terminal(BLE_HS_EDONE);
    for (unsigned i = 0; i < SERVICE_COUNT; ++i) {
      uint16_t base = 1 + 40 * i;
      assert(pending.kind == CHR && pending.start == base && pending.end == base + 39);
      for (unsigned j = 0; j < CHARS_PER_SERVICE; ++j)
        characteristic(base + 1 + 8 * j, base + 2 + 8 * j, 0x2a19);
      terminal(BLE_HS_EDONE);
    }
    for (unsigned i = 0; i < SERVICE_COUNT; ++i) {
      uint16_t base = 1 + 40 * i;
      for (unsigned j = 0; j < CHARS_PER_SERVICE; ++j) {
        assert(pending.kind == DSC && pending.start == base + 1 + 8 * j);
        for (unsigned k = 0; k < DESCS_PER_CHAR; ++k)
          descriptor(base + 3 + 8 * j + k, 0x2901);
        terminal(BLE_HS_EDONE);
      }
      assert(emitted == i + 1);
      assert(services[i]->att_handle == base);
      assert(services[i]->num_characteristics == CHARS_PER_SERVICE);
      assert(services[i]->num_descriptors == CHARS_PER_SERVICE * DESCS_PER_CHAR);
      if (i + 1 < SERVICE_COUNT) {
        assert(!completions && !queue_done);
        assert(list_count(((GATTServiceDiscoveryContext *)pending.arg)->services) ==
               SERVICE_COUNT - i - 1);
      }
    }
    assert(!completed_error);
    check_done(SERVICE_COUNT, 1);
  } else if (!strcmp(test, "empty-services")) {
    start();
    service(1, 10, 1);
    service(20, 30, 2);
    service(40, 50, 3);
    terminal(BLE_HS_EDONE);
    terminal(BLE_HS_EDONE);
    characteristic(21, 22, 4);
    terminal(BLE_HS_EDONE);
    terminal(BLE_HS_EDONE);
    assert(pending.kind == DSC && emitted == 1 && services[0]->num_characteristics == 0);
    terminal(BLE_HS_EDONE);
    check_done(3, 1);
  } else if (!strcmp(test, "no-services")) {
    start();
    terminal(BLE_HS_EDONE);
    check_done(0, 1);
  } else if (!strcmp(test, "all-empty")) {
    start();
    service(1, 10, 1);
    service(20, 30, 2);
    terminal(BLE_HS_EDONE);
    terminal(BLE_HS_EDONE);
    terminal(BLE_HS_EDONE);
    check_done(2, 1);
  } else if (!strcmp(test, "range")) {
    start();
    service(0, 10, 1);
    service(990, 1001, 2);
    terminal(BLE_HS_EDONE);
    check_done(0, 1);
  } else if (!strncmp(test, "immediate-", 10)) {
    fail_kind = !strcmp(test + 10, "services") ? SVC : !strcmp(test + 10, "chars") ? CHR : DSC;
    start();
    if (fail_kind != SVC) {
      service(1, 10, 1);
      terminal(BLE_HS_EDONE);
    }
    if (fail_kind == DSC) {
      characteristic(2, 3, 2);
      terminal(BLE_HS_EDONE);
    }
    assert(completions == 1 && completed_error == (enum pbl_bt_errno)(100 + fail_status) && !live &&
           !pending.kind);
    assert(queue_done ==
           (fail_kind != SVC)); // Initial start failure is retired by the queue itself.
  } else if (!strncmp(test, "cancel-", 7)) {
    start();
    if (strcmp(test + 7, "services")) {
      service(1, 10, 1);
      terminal(BLE_HS_EDONE);
    }
    if (!strcmp(test + 7, "descriptors")) {
      characteristic(2, 3, 2);
      terminal(BLE_HS_EDONE);
    }
    assert(pbl_bt_gatt_stop_discovery(&connection) == PBL_BT_ERRNO_OK);
    check_done(0, 0);
    assert(!s_discovery_stopped.count);
  } else if (!strncmp(test, "disconnect-", 11)) {
    one_service(false);
    valid = false;
    terminal(!strcmp(test + 11, "error") ? BLE_HS_ENOTCONN : BLE_HS_EDONE);
    check_done(0, 0);
  } else if (!strcmp(test, "timeout") || !strcmp(test, "att-error")) {
    one_service(false);
    terminal(!strcmp(test, "timeout") ? BLE_HS_ETIMEOUT : 5);
    assert(completed_error ==
           (!strcmp(test, "timeout") ? PBL_BT_ERRNO_SERVICE_DISCOVERY_TIMEOUT : 105));
    check_done(0, 1);
  } else if (!strncmp(test, "subscribe-", 10)) {
    write_status = !strcmp(test + 10, "immediate") ? 17 : 0;
    one_service(true);
    terminal(BLE_HS_EDONE);
    assert(writes == 1 && queue_done == 1 && completions == 1 && emitted == 1);
    if (!write_status) {
      struct ble_gatt_error e = {!strcmp(test + 10, "error") ? 5 : 0};
      if (!strcmp(test + 10, "disconnected"))
        valid = false;
      assert(!write_cb(42, &e, NULL, write_arg));
      write_cb = NULL;
      assert(changed == (valid && !e.status));
    }
    check_done(1, 1);
  } else if (!strcmp(test, "queued-disconnect")) {
    struct pbl_bt_att_handle_range range = {1, 10};
    pbl_bt_gatt_start_discovery_range(&connection, &range);
    valid = false;
    run_queued();
    assert(!live && !completions && !queue_done && !pending.kind);
  } else {
    assert(!"unknown scenario");
  }
  return 0;
}
