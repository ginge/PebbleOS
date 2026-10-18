/* SPDX-License-Identifier: Apache-2.0 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <pbl/drivers/flash.h>
#include <pbl/services/filesystem/pfs.h>
#include <pbl/services/i18n/i18n.h>

#include <applib/graphics/text_resources.h>
#include <clar.h>
#include <fake_spi_flash.h>
#include <flash_region/flash_region.h>
#include <kernel/events.h>
#include <resource/resource.h>
#include <resource/resource_ids.auto.h>
#include <resource/resource_storage_impl.h>
#include <resource/resource_version.auto.h>
#include <sys/mman.h>
#include <sys/stat.h>

static void *s_bank;
static FontInfo s_font;
static bool s_english;
static unsigned s_changes;

bool shell_prefs_get_language_english(void) {
  return s_english;
}

void shell_prefs_set_language_english(bool english) {
  s_english = english;
}

void launcher_task_add_callback(void (*callback)(void *), void *data) {
  callback(data);
}

void event_put(PebbleEvent *event) {
}

static uint8_t *prv_load_fixture(const char *name, size_t *size) {
  char path[512];
  snprintf(path, sizeof(path), "%s/resources/%s", CLAR_FIXTURE_PATH, name);
  struct stat info;
  cl_assert_equal_i(stat(path, &info), 0);
  *size = info.st_size;
  uint8_t *bytes = malloc(*size);
  cl_assert(bytes);
  FILE *file = fopen(path, "rb");
  cl_assert(file);
  cl_assert_equal_i(fread(bytes, 1, *size, file), *size);
  cl_assert_equal_i(fclose(file), 0);
  return bytes;
}

static void prv_write_file(const char *name, const void *bytes, size_t size) {
  // PutBytes removes the prior file before allocating its replacement.
  pfs_remove(name);
  int fd = pfs_open(name, OP_FLAG_WRITE, FILE_TYPE_STATIC, size);
  cl_assert(fd >= 0);
  cl_assert_equal_i(pfs_write(fd, bytes, size), size);
  pfs_close(fd);
}

static void prv_install(const char *fixture, const char *name) {
  size_t size;
  uint8_t *bytes = prv_load_fixture(fixture, &size);
  prv_write_file(name, bytes, size);
  free(bytes);
}

static void prv_changed(void *data) {
  cl_assert(data == &s_changes);
  ++s_changes;
}

static void prv_check_base_font(void) {
  cl_assert(resource_is_valid(SYSTEM_APP, RESOURCE_ID_GOTHIC_18));
  cl_assert(text_resources_init_font(SYSTEM_APP, RESOURCE_ID_GOTHIC_18,
                                     RESOURCE_ID_GOTHIC_18_EXTENDED, &s_font));
  cl_assert(s_font.loaded);
  cl_assert(s_font.base.md.number_of_glyphs > 0);
}

void test_t5ai_system_resources__initialize(void) {
  const size_t bank_size =
      FLASH_REGION_SYSTEM_RESOURCES_BANK_0_END - FLASH_REGION_SYSTEM_RESOURCES_BANK_0_BEGIN;
  s_bank = mmap((void *)(uintptr_t)FLASH_REGION_SYSTEM_RESOURCES_BANK_0_BEGIN, bank_size,
                PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
  cl_assert(s_bank != MAP_FAILED);
  cl_assert(s_bank == (void *)(uintptr_t)FLASH_REGION_SYSTEM_RESOURCES_BANK_0_BEGIN);
  memset(s_bank, 0xff, bank_size);
  size_t size;
  uint8_t *bytes = prv_load_fixture("system_resources_" PLATFORM_NAME ".pbpack", &size);
  cl_assert(size <= bank_size);
  memcpy(s_bank, bytes, size);
  fake_spi_flash_init(FLASH_REGION_BASE_ADDRESS, BOARD_NOR_FLASH_SIZE);
  flash_write_bytes(bytes, FLASH_REGION_SYSTEM_RESOURCES_BANK_0_BEGIN, size);
  free(bytes);
  pfs_init(false);
  pfs_format(true);
  resource_init();
  memset(&s_font, 0, sizeof(s_font));
  s_changes = 0;
  s_english = false;
  i18n_set_resource(RESOURCE_ID_STRINGS);
}

void test_t5ai_system_resources__cleanup(void) {
  if (s_font.extension_changed_cb) {
    resource_unwatch(s_font.extension_changed_cb);
  }
  s_english = true;
  i18n_set_resource(0);
  fake_spi_flash_cleanup();
  cl_assert_equal_i(munmap(s_bank, FLASH_REGION_SYSTEM_RESOURCES_BANK_0_END -
                                       FLASH_REGION_SYSTEM_RESOURCES_BANK_0_BEGIN),
                    0);
}

void test_t5ai_system_resources__empty_pfs_uses_english_and_base_fonts(void) {
  const uint32_t writes = fake_flash_write_count();
  cl_assert(resource_init_app(SYSTEM_APP, &SYSTEM_RESOURCE_VERSION));
  cl_assert_equal_s(i18n_get_locale(), "en_US");
  cl_assert_equal_s(i18n_get("Music", __FILE__), "Music");
  i18n_free_all(__FILE__);
  cl_assert_equal_i(resource_size(SYSTEM_APP, RESOURCE_ID_STRINGS), 0);
  prv_check_base_font();
  cl_assert(!s_font.extended);
  cl_assert_equal_i(fake_flash_write_count(), writes);
}

void test_t5ai_system_resources__install_and_remove_language_without_reboot(void) {
  prv_install("fr_FR.pbpack", "lang");
  cl_assert(resource_is_valid(SYSTEM_APP, RESOURCE_ID_STRINGS));
  cl_assert_equal_s(i18n_get_locale(), "fr_FR");
  cl_assert_equal_s(i18n_get("Music", __FILE__), "Musique");
  i18n_free_all(__FILE__);
  s_english = true;
  i18n_set_resource(RESOURCE_ID_STRINGS);
  cl_assert_equal_s(i18n_get_locale(), "en_US");
  s_english = false;
  i18n_set_resource(RESOURCE_ID_STRINGS);
  cl_assert_equal_s(i18n_get_locale(), "fr_FR");
  cl_assert_equal_i(pfs_remove("lang"), 0);
  cl_assert_equal_s(i18n_get_locale(), "en_US");
  prv_check_base_font();
  cl_assert(!s_font.extended);
}

void test_t5ai_system_resources__watch_before_creation_and_unwatch(void) {
  ResourceCallbackHandle watch =
      resource_watch(SYSTEM_APP, RESOURCE_ID_PUG, prv_changed, &s_changes);
  cl_assert(watch);
  prv_install("pug.pbpack", "pug");
  cl_assert_equal_i(s_changes, 1);
  cl_assert(resource_is_valid(SYSTEM_APP, RESOURCE_ID_PUG));
  cl_assert(resource_size(SYSTEM_APP, RESOURCE_ID_PUG) > 0);
  resource_unwatch(watch);
  prv_install("pug.pbpack", "pug");
  cl_assert_equal_i(s_changes, 1);
}

void test_t5ai_system_resources__font_extension_creation_and_removal_invalidates(void) {
  prv_check_base_font();
  cl_assert(!s_font.extended);
  prv_install("zh_CN.pbpack", "lang");
  cl_assert(!s_font.loaded);
  prv_check_base_font();
  cl_assert(s_font.extended);
  cl_assert(resource_is_valid(SYSTEM_APP, RESOURCE_ID_GOTHIC_18_EXTENDED));
  cl_assert_equal_i(pfs_remove("lang"), 0);
  cl_assert(!s_font.loaded);
  prv_check_base_font();
  cl_assert(!s_font.extended);
}

void test_t5ai_system_resources__corrupt_language_retains_english_and_base_fonts(void) {
  size_t size;
  uint8_t *bytes = prv_load_fixture("fr_FR.pbpack", &size);
  bytes[size - 1] ^= 1;
  prv_write_file("lang", bytes, size);
  free(bytes);
  cl_assert(!resource_is_valid(SYSTEM_APP, RESOURCE_ID_STRINGS));
  cl_assert_equal_s(i18n_get_locale(), "en_US");
  prv_check_base_font();
  prv_install("fr_FR.pbpack", "lang");
  cl_assert(resource_is_valid(SYSTEM_APP, RESOURCE_ID_STRINGS));
  cl_assert_equal_s(i18n_get_locale(), "fr_FR");
}

void test_t5ai_system_resources__truncated_language_can_be_replaced(void) {
  const uint8_t partial[] = {1, 2, 3};
  prv_write_file("lang", partial, sizeof(partial));
  cl_assert(!resource_is_valid(SYSTEM_APP, RESOURCE_ID_STRINGS));
  cl_assert_equal_s(i18n_get_locale(), "en_US");
  prv_check_base_font();
  prv_install("fr_FR.pbpack", "lang");
  cl_assert_equal_s(i18n_get_locale(), "fr_FR");
}

void test_t5ai_system_resources__app_resources_remain_separate_from_system_files(void) {
  const ResAppNum app = 42;
  char name[32];
  resource_storage_get_file_name(name, sizeof(name), app);
  prv_install("app_resources.pbpack", name);
  cl_assert(resource_init_app(app, nullptr));
  cl_assert(resource_is_valid(app, 1));
  cl_assert(resource_size(app, 1) > 0);
  prv_install("fr_FR.pbpack", "lang");
  cl_assert_equal_s(i18n_get_locale(), "fr_FR");
  cl_assert(resource_is_valid(app, 1));
}

void test_t5ai_system_resources__stored_language_survives_resource_reinitialization(void) {
  prv_install("fr_FR.pbpack", "lang");
  s_english = true;
  i18n_set_resource(0);
  resource_init();
  cl_assert(resource_init_app(SYSTEM_APP, &SYSTEM_RESOURCE_VERSION));
  s_english = false;
  i18n_set_resource(RESOURCE_ID_STRINGS);
  cl_assert_equal_s(i18n_get_locale(), "fr_FR");
  cl_assert_equal_s(i18n_get("Music", __FILE__), "Musique");
  i18n_free_all(__FILE__);
  prv_check_base_font();
}
