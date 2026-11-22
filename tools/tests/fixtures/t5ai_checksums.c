/* SPDX-License-Identifier: Apache-2.0 */
#define _POSIX_C_SOURCE 200809L
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#include <pbl/crc/crc.h>
#include <pbl/soc/t5ai/mipc.h>

uint32_t ap_flash(const void *, size_t), ap_saradc(const void *, size_t);
uint32_t vendor_flash(const void *, size_t), vendor_saradc(const void *, size_t);
uint8_t vendor_mipc(uint8_t *, size_t, uint8_t);

// Evaluation adapter only: the native API complements its running state/result.
static uint32_t native_flash(const void *data, size_t size) {
  return size == 0 ? 0 : ~pbl_crc32(UINT32_MAX, data, size);
}
static uint8_t data[UINT16_MAX + 4];
static volatile uint32_t sink;
static void check(const uint8_t *p, size_t n) {
  const uint32_t expected = vendor_flash(p, n);
  assert(ap_flash(p, n) == expected);
  assert(native_flash(p, n) == expected);
}
static void vectors(void) {
  assert(ap_flash("123456789", 9) == 0x2dfd2d88u);
  assert(ap_saradc("123456789", 9) == 0x47ffc2a2u);
  assert(t5ai_mipc_crc8("123456789", 9, T5AI_MIPC_CRC_INITIAL) == 0x7fu);
  T5AIMIPCData message =
      {.result = 1, .handle = 2, .semaphore = 3, .data = 4, .size = 5, .flags = 6};
  const uint8_t checksum = t5ai_mipc_data_crc(&message);
  assert(checksum == vendor_mipc((uint8_t *)&message.result,
                                 sizeof(message) - sizeof(message.list) - sizeof(message.result),
                                 T5AI_MIPC_CRC_INITIAL));
  message.list.next ^= 1;
  message.flags ^= 1;
  assert(t5ai_mipc_data_crc(&message) == checksum);
  message.result ^= 1;
  assert(t5ai_mipc_data_crc(&message) != checksum);
  assert(native_flash(NULL, 0) == 0);
  check(NULL, 0);
  for (unsigned pattern = 0; pattern < 4; ++pattern) {
    for (unsigned i = 0; i < sizeof(data); ++i)
      data[i] = pattern == 0 ? 0 : pattern == 1 ? 255 : pattern == 2 ? i : i * 73u + i / 7u;
    for (unsigned offset = 0; offset < 4; ++offset) {
      for (unsigned n = 0; n <= 2048; ++n)
        check(data + offset, n);
      const size_t lengths[] = {4095, 4096, 4097, 8192, 32768, UINT16_MAX};
      for (unsigned i = 0; i < sizeof(lengths) / sizeof(lengths[0]); ++i)
        check(data + offset, lengths[i]);
      for (unsigned n = 0; n <= 64; ++n)
        assert(ap_saradc(data + offset, n) == vendor_saradc(data + offset, n));
    }
  }
  for (unsigned seed = 0; seed <= UINT8_MAX; ++seed) {
    for (unsigned n = 0; n <= 128; ++n)
      assert(t5ai_mipc_crc8(data, n, seed) == vendor_mipc(data, n, seed));
    data[0] = seed;
    check(data, 1);
  }
  puts("vectors pass: flash/AP/native, SARADC full accumulator, MIPC all seeds");
}
static uint64_t now(void) {
  struct timespec t;
  assert(clock_gettime(CLOCK_MONOTONIC, &t) == 0);
  return (uint64_t)t.tv_sec * 1000000000u + t.tv_nsec;
}
static uint64_t measure(uint32_t (*function)(const void *, size_t), size_t n) {
  const unsigned iterations = 1u + 8u * 1024u * 1024u / n;
  const uint64_t start = now();
  for (unsigned i = 0; i < iterations; ++i)
    sink ^= function(data, n);
  return (now() - start) / iterations;
}
static int compare(const void *a, const void *b) {
  const uint64_t x = *(const uint64_t *)a, y = *(const uint64_t *)b;
  return (x > y) - (x < y);
}
static void benchmark(void) {
  for (unsigned i = 0; i < sizeof(data); ++i)
    data[i] = i * 73u + i / 7u;
  const size_t lengths[] = {32, 256, 2048, 4096, UINT16_MAX};
  puts("bytes,ap_table_ns,native_adapter_ns,ratio");
  for (unsigned i = 0; i < sizeof(lengths) / sizeof(lengths[0]); ++i) {
    uint64_t old[5], candidate[5];
    for (unsigned trial = 0; trial < 5; ++trial) {
      old[trial] = measure(ap_flash, lengths[i]);
      candidate[trial] = measure(native_flash, lengths[i]);
    }
    qsort(old, 5, sizeof(old[0]), compare);
    qsort(candidate, 5, sizeof(candidate[0]), compare);
    printf("%zu,%llu,%llu,%.3f\n", lengths[i], (unsigned long long)old[2],
           (unsigned long long)candidate[2], (double)candidate[2] / old[2]);
  }
}
int main(int argc, char **argv) {
  if (argc > 1)
    benchmark();
  else
    vectors();
  return 0;
}
