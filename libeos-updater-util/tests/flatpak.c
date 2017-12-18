/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*-
 *
 * Copyright © 2017 Endless Mobile, Inc.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 *
 * Authors:
 *  - Sam Spilsbury <sam@endlessm.com>
 *  - Philip Withnall <withnall@endlessm.com>
 */

#include <flatpak.h>
#include <glib.h>
#include <libeos-updater-util/flatpak.h>
#include <libeos-updater-util/types.h>
#include <locale.h>

typedef struct
{
  EuuFlatpakRemoteRefActionType type;
  FlatpakRefKind kind;
  const gchar *app_id;
  gint32 serial;
} FlatpakToInstallEntry;

typedef struct
{
  const gchar *name;
  FlatpakToInstallEntry *entries;
  guint n_entries;
} FlatpakToInstallFile;

typedef struct
{
  FlatpakToInstallFile *files;
  guint n_files;
} FlatpakToInstallDirectory;

static EuuFlatpakRemoteRefAction *
flatpak_to_install_entry_to_remote_ref_action (const gchar           *source,
                                               FlatpakToInstallEntry *entry)
{
  g_autoptr(FlatpakRef) ref = g_object_new (FLATPAK_TYPE_REF,
                                            "name", entry->app_id,
                                            "kind", entry->kind,
                                            NULL);
  g_autoptr(EuuFlatpakLocationRef) location_ref = euu_flatpak_location_ref_new (ref, "none", NULL);

  return euu_flatpak_remote_ref_action_new (entry->type,
                                            location_ref,
                                            source,
                                            entry->serial);
}

static GPtrArray *
flatpak_to_install_file_to_actions (FlatpakToInstallFile *file)
{
  g_autoptr(GPtrArray) array = g_ptr_array_new_full (file->n_entries,
                                                     (GDestroyNotify) euu_flatpak_remote_ref_action_unref);
  gsize i;

  for (i = 0; i < file->n_entries; ++i)
    {
      g_ptr_array_add (array, flatpak_to_install_entry_to_remote_ref_action (file->name,
                                                                             &file->entries[i]));
    }

  return g_steal_pointer (&array);
}

static GHashTable *
flatpak_to_install_directory_to_hash_table (FlatpakToInstallDirectory *directory)
{
  g_autoptr(GHashTable) ref_actions_in_directory = g_hash_table_new_full (g_str_hash,
                                                                          g_str_equal,
                                                                          g_free,
                                                                          (GDestroyNotify) euu_flatpak_remote_ref_actions_file_free);
  gsize i;

  for (i = 0; i < directory->n_files; ++i)
    {
      g_autoptr(GPtrArray) remote_ref_actions = flatpak_to_install_file_to_actions (&directory->files[i]);
      g_hash_table_insert (ref_actions_in_directory,
                           g_strdup (directory->files[i].name),
                           euu_flatpak_remote_ref_actions_file_new (remote_ref_actions, 0));
    }

  return euu_hoist_flatpak_remote_ref_actions (ref_actions_in_directory);
}

/* Test that actions 'install', then 'update' get compressed as 'install' */
static void
test_compress_install_update_as_install (void)
{
  FlatpakToInstallEntry entries[] = {
    { EUU_FLATPAK_REMOTE_REF_ACTION_INSTALL, FLATPAK_REF_KIND_APP, "org.test.Test", 1 },
    { EUU_FLATPAK_REMOTE_REF_ACTION_UPDATE, FLATPAK_REF_KIND_APP, "org.test.Test", 2 }
  };
  FlatpakToInstallFile files[] = {
    { "autoinstall", entries, G_N_ELEMENTS (entries) }
  };
  FlatpakToInstallDirectory directory = { files, G_N_ELEMENTS (files) };
  g_autoptr(GHashTable) uncompressed_ref_actions_table = flatpak_to_install_directory_to_hash_table (&directory);
  g_autoptr(GPtrArray) flattened_actions_list = euu_flatten_flatpak_ref_actions_table (uncompressed_ref_actions_table);

  g_assert_cmpuint (flattened_actions_list->len, ==, 1);
  g_assert_cmpint (((EuuFlatpakRemoteRefAction *) g_ptr_array_index (flattened_actions_list, 0))->type, ==,
                   EUU_FLATPAK_REMOTE_REF_ACTION_INSTALL);
}

/* Test that actions 'uninstall', then 'update' get compressed as 'uninstall' */
static void
test_compress_uninstall_update_as_uninstall (void)
{
  FlatpakToInstallEntry entries[] = {
    { EUU_FLATPAK_REMOTE_REF_ACTION_UNINSTALL, FLATPAK_REF_KIND_APP, "org.test.Test", 1 },
    { EUU_FLATPAK_REMOTE_REF_ACTION_UPDATE, FLATPAK_REF_KIND_APP, "org.test.Test", 2 }
  };
  FlatpakToInstallFile files[] = {
    { "autoinstall", entries, G_N_ELEMENTS (entries) }
  };
  FlatpakToInstallDirectory directory = { files, G_N_ELEMENTS (files) };
  g_autoptr(GHashTable) uncompressed_ref_actions_table = flatpak_to_install_directory_to_hash_table (&directory);
  g_autoptr(GPtrArray) flattened_actions_list = euu_flatten_flatpak_ref_actions_table (uncompressed_ref_actions_table);

  g_assert_cmpuint (flattened_actions_list->len, ==, 1);
  g_assert_cmpint (((EuuFlatpakRemoteRefAction *) g_ptr_array_index (flattened_actions_list, 0))->type, ==,
                   EUU_FLATPAK_REMOTE_REF_ACTION_UNINSTALL);
}

/* Test that actions 'install', then 'uninstall' get compressed as 'uninstall' */
static void
test_compress_install_uninstall_as_uninstall (void)
{
  FlatpakToInstallEntry entries[] = {
    { EUU_FLATPAK_REMOTE_REF_ACTION_INSTALL, FLATPAK_REF_KIND_APP, "org.test.Test", 1 },
    { EUU_FLATPAK_REMOTE_REF_ACTION_UNINSTALL, FLATPAK_REF_KIND_APP, "org.test.Test", 2 }
  };
  FlatpakToInstallFile files[] = {
    { "autoinstall", entries, G_N_ELEMENTS (entries) }
  };
  FlatpakToInstallDirectory directory = { files, G_N_ELEMENTS (files) };
  g_autoptr(GHashTable) uncompressed_ref_actions_table = flatpak_to_install_directory_to_hash_table (&directory);
  g_autoptr(GPtrArray) flattened_actions_list = euu_flatten_flatpak_ref_actions_table (uncompressed_ref_actions_table);

  g_assert_cmpuint (flattened_actions_list->len, ==, 1);
  g_assert_cmpint (((EuuFlatpakRemoteRefAction *) g_ptr_array_index (flattened_actions_list, 0))->type, ==,
                   EUU_FLATPAK_REMOTE_REF_ACTION_UNINSTALL);
}

/* Test that actions 'install', then 'uninstall', then 'install' get compressed
 * as 'install' */
static void
test_compress_install_uninstall_install_as_install (void)
{
  FlatpakToInstallEntry entries[] = {
    { EUU_FLATPAK_REMOTE_REF_ACTION_INSTALL, FLATPAK_REF_KIND_APP, "org.test.Test", 1 },
    { EUU_FLATPAK_REMOTE_REF_ACTION_UNINSTALL, FLATPAK_REF_KIND_APP, "org.test.Test", 2 },
    { EUU_FLATPAK_REMOTE_REF_ACTION_INSTALL, FLATPAK_REF_KIND_APP, "org.test.Test", 3 }
  };
  FlatpakToInstallFile files[] = {
    { "autoinstall", entries, G_N_ELEMENTS (entries) }
  };
  FlatpakToInstallDirectory directory = { files, G_N_ELEMENTS (files) };
  g_autoptr(GHashTable) uncompressed_ref_actions_table = flatpak_to_install_directory_to_hash_table (&directory);
  g_autoptr(GPtrArray) flattened_actions_list = euu_flatten_flatpak_ref_actions_table (uncompressed_ref_actions_table);

  g_assert_cmpuint (flattened_actions_list->len, ==, 1);
  g_assert_cmpint (((EuuFlatpakRemoteRefAction *) g_ptr_array_index (flattened_actions_list, 0))->type, ==,
                   EUU_FLATPAK_REMOTE_REF_ACTION_INSTALL);
}

/* Test that actions 'update', then 'update' get compressed as 'update' */
static void
test_compress_update_update_as_update (void)
{
  FlatpakToInstallEntry entries[] = {
    { EUU_FLATPAK_REMOTE_REF_ACTION_UPDATE, FLATPAK_REF_KIND_APP, "org.test.Test", 1 },
    { EUU_FLATPAK_REMOTE_REF_ACTION_UPDATE, FLATPAK_REF_KIND_APP, "org.test.Test", 2 }
  };
  FlatpakToInstallFile files[] = {
    { "autoinstall", entries, G_N_ELEMENTS (entries) }
  };
  FlatpakToInstallDirectory directory = { files, G_N_ELEMENTS (files) };
  g_autoptr(GHashTable) uncompressed_ref_actions_table = flatpak_to_install_directory_to_hash_table (&directory);
  g_autoptr(GPtrArray) flattened_actions_list = euu_flatten_flatpak_ref_actions_table (uncompressed_ref_actions_table);

  g_assert_cmpuint (flattened_actions_list->len, ==, 1);
  g_assert_cmpint (((EuuFlatpakRemoteRefAction *) g_ptr_array_index (flattened_actions_list, 0))->type, ==,
                   EUU_FLATPAK_REMOTE_REF_ACTION_UPDATE);
}

/* Test that actions 'install', then 'install' get compressed as 'install' */
static void
test_compress_install_install_as_install (void)
{
  FlatpakToInstallEntry entries[] = {
    { EUU_FLATPAK_REMOTE_REF_ACTION_INSTALL, FLATPAK_REF_KIND_APP, "org.test.Test", 1 },
    { EUU_FLATPAK_REMOTE_REF_ACTION_INSTALL, FLATPAK_REF_KIND_APP, "org.test.Test", 2 }
  };
  FlatpakToInstallFile files[] = {
    { "autoinstall", entries, G_N_ELEMENTS (entries) }
  };
  FlatpakToInstallDirectory directory = { files, G_N_ELEMENTS (files) };
  g_autoptr(GHashTable) uncompressed_ref_actions_table = flatpak_to_install_directory_to_hash_table (&directory);
  g_autoptr(GPtrArray) flattened_actions_list = euu_flatten_flatpak_ref_actions_table (uncompressed_ref_actions_table);

  g_assert_cmpuint (flattened_actions_list->len, ==, 1);
  g_assert_cmpint (((EuuFlatpakRemoteRefAction *) g_ptr_array_index (flattened_actions_list, 0))->type, ==,
                   EUU_FLATPAK_REMOTE_REF_ACTION_INSTALL);
}

/* Test the autoinstall file parser handles various different constructs (valid
 * and erroneous) in the format, returning success or an error when appropriate. */
static void
test_parse_autoinstall_file (void)
{
  const struct
    {
      const gchar *data;
      gsize expected_n_actions;
      GQuark expected_error_domain;
      gint expected_error_code;
    } vectors[] =
    {
      { "", 0, 0, 0 },
      { "'a json string'", 0,
        EOS_UPDATER_ERROR, EOS_UPDATER_ERROR_MALFORMED_AUTOINSTALL_SPEC },
      { "not valid JSON", 0,
        EOS_UPDATER_ERROR, EOS_UPDATER_ERROR_MALFORMED_AUTOINSTALL_SPEC },

      { "[]", 0, 0, 0 },
      { "[{ 'action': 'install', 'serial': 2017100100, 'ref-kind': 'app', "
        "   'name': 'org.example.MyApp', 'collection-id': 'com.endlessm.Apps', "
        "   'remote': 'eos-apps' }]", 1, 0, 0 },
      { "[{ 'action': 'uninstall', 'serial': 2017100101, 'ref-kind': 'app', "
        "   'name': 'org.example.OutdatedApp', 'collection-id': 'com.endlessm.Apps', "
        "   'remote': 'eos-apps' }]", 1, 0, 0 },
      { "[{ 'action': 'install', 'serial': 2017100500, 'ref-kind': 'runtime', "
        "   'name': 'org.example.PreinstalledRuntime', 'collection-id': 'com.endlessm.Runtimes', "
        "   'remote': 'eos-runtimes' }]", 1, 0, 0 },
      { "[{ 'action': 'install', 'serial': 2017110100, 'ref-kind': 'runtime', "
        "   'name': 'org.example.NVidiaRuntime', 'collection-id': 'com.endlessm.Runtimes', "
        "   'remote': 'eos-runtimes' }]", 1, 0, 0 },
      { "[{ 'action': 'install', 'serial': 2017110200, 'ref-kind': 'app', "
        "   'name': 'org.example.IndonesiaNonArmGame', 'collection-id': 'org.example.Apps', "
        "   'remote': 'example-apps', "
        "   'filters': { 'locale': ['nonexistent'], '~architecture': ['armhf'] }}]",
        0, 0, 0 },
      { "[{ 'action': 'install', 'serial': 2017110200, 'ref-kind': 'app', "
        "   'name': 'org.example.IndonesiaNonArmGame', 'collection-id': 'org.example.Apps', "
        "   'remote': 'example-apps', "
        "   'filters': {}}]", 1, 0, 0 },
      { "[{ 'action': 'install', 'serial': 2017110200, 'ref-kind': 'app', "
        "   'name': 'org.example.IndonesiaNonArmGame', 'collection-id': 'org.example.Apps', "
        "   'remote': 'example-apps', "
        "   'filters': { '~locale': [], 'architecture': [] }}]", 0, 0, 0 },
      { "[{ 'action': 'update', 'serial': 2017100101, 'ref-kind': 'app', "
        "   'name': 'org.example.OutdatedApp', 'collection-id': 'com.endlessm.Apps', "
        "   'remote': 'eos-apps' }]", 1, 0, 0 },

      { "[{ 'action': 123, 'serial': 2017100100, 'ref-kind': 'app', "
        "   'name': 'org.example.MyApp', 'remote': 'eos-apps' }]", 0,
        EOS_UPDATER_ERROR, EOS_UPDATER_ERROR_MALFORMED_AUTOINSTALL_SPEC },
      { "[{ 'action': 'install', 'serial': 2017100100, 'ref-kind': 'invalid', "
        "   'name': 'org.example.MyApp', 'collection-id': 'com.endlessm.Apps', "
        "   'remote': 'eos-apps' }]", 0,
        EOS_UPDATER_ERROR, EOS_UPDATER_ERROR_MALFORMED_AUTOINSTALL_SPEC },
      { "[{ 'action': 'install', 'serial': 2017100100, 'ref-kind': 123, "
        "   'name': 'org.example.MyApp', 'collection-id': 'com.endlessm.Apps', "
        "   'remote': 'eos-apps' }]", 0,
        EOS_UPDATER_ERROR, EOS_UPDATER_ERROR_MALFORMED_AUTOINSTALL_SPEC },
      { "[{}]", 0,
        EOS_UPDATER_ERROR, EOS_UPDATER_ERROR_MALFORMED_AUTOINSTALL_SPEC },
      { "['a string']", 0,
        EOS_UPDATER_ERROR, EOS_UPDATER_ERROR_MALFORMED_AUTOINSTALL_SPEC },
      { "[{ 'action': 'install' }]", 0,
        EOS_UPDATER_ERROR, EOS_UPDATER_ERROR_MALFORMED_AUTOINSTALL_SPEC },
      { "[{ 'action': 'install', 'serial': 2017100100 }]", 0,
        EOS_UPDATER_ERROR, EOS_UPDATER_ERROR_MALFORMED_AUTOINSTALL_SPEC },
      { "[{ 'action': 'install', 'serial': 2017100100, 'ref-kind': 'app', "
        "   'name': 123, 'collection-id': 'com.endlessm.Apps', "
        "   'remote': 'eos-apps' }]", 0,
        EOS_UPDATER_ERROR, EOS_UPDATER_ERROR_MALFORMED_AUTOINSTALL_SPEC },
      { "[{ 'action': 'install', 'serial': 2017100100, 'ref-kind': 'app', "
        "   'name': 'org.example.MyApp', 'collection-id': 123, "
        "   'remote': 'eos-apps' }]", 0,
        EOS_UPDATER_ERROR, EOS_UPDATER_ERROR_MALFORMED_AUTOINSTALL_SPEC },
      { "[{ 'action': 'install', 'serial': 2017100100, 'ref-kind': 'app', "
        "   'name': 'org.example.MyApp', 'collection-id': 'com.endlessm.Apps', "
        "   'remote': 123 }]", 0,
        EOS_UPDATER_ERROR, EOS_UPDATER_ERROR_MALFORMED_AUTOINSTALL_SPEC },
      { "[{ 'action': 'install', 'serial': 2147483648, 'ref-kind': 'app', "
        "   'name': 'org.example.MyApp', 'collection-id': 'com.endlessm.Apps', "
        "   'remote': 'eos-apps' }]", 0,
        EOS_UPDATER_ERROR, EOS_UPDATER_ERROR_MALFORMED_AUTOINSTALL_SPEC },
      { "[{ 'action': 'install', 'serial': -2147483649, 'ref-kind': 'app', "
        "   'name': 'org.example.MyApp', 'collection-id': 'com.endlessm.Apps', "
        "   'remote': 'eos-apps' }]", 0,
        EOS_UPDATER_ERROR, EOS_UPDATER_ERROR_MALFORMED_AUTOINSTALL_SPEC },

      { "[{ 'action': 'uninstall' }]", 0,
        EOS_UPDATER_ERROR, EOS_UPDATER_ERROR_MALFORMED_AUTOINSTALL_SPEC },
      { "[{ 'action': 'uninstall', 'serial': 2017100100 }]", 0,
        EOS_UPDATER_ERROR, EOS_UPDATER_ERROR_MALFORMED_AUTOINSTALL_SPEC },

      { "[{ 'action': 'update' }]", 0,
        EOS_UPDATER_ERROR, EOS_UPDATER_ERROR_MALFORMED_AUTOINSTALL_SPEC },
      { "[{ 'action': 'update', 'serial': 2017100100 }]", 0,
        EOS_UPDATER_ERROR, EOS_UPDATER_ERROR_MALFORMED_AUTOINSTALL_SPEC },

      { "[{ 'action': 'install', 'serial': 2017110200, 'ref-kind': 'app', "
        "   'name': 'org.example.IndonesiaNonArmGame', 'collection-id': 'org.example.Apps', "
        "   'remote': 'example-apps', "
        "   'filters': 'not an object' }]",
        0, EOS_UPDATER_ERROR, EOS_UPDATER_ERROR_MALFORMED_AUTOINSTALL_SPEC },
      { "[{ 'action': 'install', 'serial': 2017110200, 'ref-kind': 'app', "
        "   'name': 'org.example.IndonesiaNonArmGame', 'collection-id': 'org.example.Apps', "
        "   'remote': 'example-apps', "
        "   'filters': { 'locale': 'not an array' }}]",
        0, EOS_UPDATER_ERROR, EOS_UPDATER_ERROR_MALFORMED_AUTOINSTALL_SPEC },
      { "[{ 'action': 'install', 'serial': 2017110200, 'ref-kind': 'app', "
        "   'name': 'org.example.IndonesiaNonArmGame', 'collection-id': 'org.example.Apps', "
        "   'remote': 'example-apps', "
        "   'filters': { 'locale': [123] }}]",
        0, EOS_UPDATER_ERROR, EOS_UPDATER_ERROR_MALFORMED_AUTOINSTALL_SPEC },
      { "[{ 'action': 'install', 'serial': 2017110200, 'ref-kind': 'app', "
        "   'name': 'org.example.IndonesiaNonArmGame', 'collection-id': 'org.example.Apps', "
        "   'remote': 'example-apps', "
        "   'filters': { 'locale': ['not allowed both'], '~locale': ['filters'] }}]",
        0, EOS_UPDATER_ERROR, EOS_UPDATER_ERROR_MALFORMED_AUTOINSTALL_SPEC },
      { "[{ 'action': 'install', 'serial': 2017110200, 'ref-kind': 'app', "
        "   'name': 'org.example.IndonesiaNonArmGame', 'collection-id': 'org.example.Apps', "
        "   'remote': 'example-apps', "
        "   'filters': { 'architecture': ['not allowed both'], '~architecture': ['filters'] }}]",
        0, EOS_UPDATER_ERROR, EOS_UPDATER_ERROR_MALFORMED_AUTOINSTALL_SPEC },
    };
  gsize i;

  for (i = 0; i < G_N_ELEMENTS (vectors); i++)
    {
      g_autoptr(GPtrArray) actions = NULL;
      g_autoptr(GError) error = NULL;

      g_test_message ("Vector %" G_GSIZE_FORMAT ": %s", i, vectors[i].data);

      actions = euu_flatpak_ref_actions_from_data (vectors[i].data, -1, "test",
                                                   NULL, &error);

      if (error != NULL)
        g_test_message ("Got error: %s", error->message);

      if (vectors[i].expected_error_domain != 0)
        {
          g_assert_error (error, vectors[i].expected_error_domain, vectors[i].expected_error_code);
          g_assert_null (actions);
        }
      else
        {
          g_assert_no_error (error);
          g_assert_nonnull (actions);
          g_assert_cmpuint (actions->len, ==, vectors[i].expected_n_actions);
        }
    }
}

/* Test that the filters on autoinstall files work correctly. */
static void
test_autoinstall_file_filters (void)
{
  const gchar *data =
      "[{ 'action': 'install', 'serial': 2017110200, 'ref-kind': 'app', "
      "   'name': 'org.example.IndonesiaNonArmGame', 'collection-id': 'org.example.Apps', "
      "   'remote': 'example-apps', "
      "   'filters': { %s }"
      "}]";

  g_autofree gchar *old_env_arch = g_strdup (g_getenv ("EOS_UPDATER_TEST_OVERRIDE_ARCHITECTURE"));
  g_autofree gchar *old_env_locales = g_strdup (g_getenv ("EOS_UPDATER_TEST_UPDATER_OVERRIDE_LOCALES"));

  const struct
    {
      const gchar *filters;
      const gchar *env_arch;
      const gchar *env_locales;
      gsize expected_n_actions;
    } vectors[] =
    {
      { "", "", "", 1 },

      { "'architecture': []", "", "", 0 },
      { "'architecture': ['arch1']", "arch1", "", 1 },
      { "'architecture': ['arch1', 'arch2']", "arch1", "", 1 },
      { "'architecture': ['arch1', 'arch2']", "arch2", "", 1 },
      { "'architecture': ['arch1', 'arch2']", "arch3", "", 0 },

      { "'~architecture': []", "", "", 1 },
      { "'~architecture': ['arch1']", "arch1", "", 0 },
      { "'~architecture': ['arch1', 'arch2']", "arch1", "", 0 },
      { "'~architecture': ['arch1', 'arch2']", "arch2", "", 0 },
      { "'~architecture': ['arch1', 'arch2']", "arch3", "", 1 },

      { "'locale': []", "", "", 0 },
      { "'locale': ['locale1']", "", "locale1", 1 },
      { "'locale': ['locale1']", "", "locale2;locale1", 1 },
      { "'locale': ['locale1', 'locale2']", "", "locale1", 1 },
      { "'locale': ['locale1', 'locale2']", "", "locale2;locale1", 1 },
      { "'locale': ['locale1', 'locale2']", "", "locale3;locale1", 1 },
      { "'locale': ['locale1', 'locale2']", "", "locale2", 1 },
      { "'locale': ['locale1', 'locale2']", "", "locale1;locale2", 1 },
      { "'locale': ['locale1', 'locale2']", "", "locale3", 0 },
      { "'locale': ['locale1', 'locale2']", "", "locale3;locale4", 0 },

      { "'~locale': []", "", "", 1 },
      { "'~locale': ['locale1']", "", "locale1", 0 },
      { "'~locale': ['locale1']", "", "locale2;locale1", 0 },
      { "'~locale': ['locale1', 'locale2']", "", "locale1", 0 },
      { "'~locale': ['locale1', 'locale2']", "", "locale2;locale1", 0 },
      { "'~locale': ['locale1', 'locale2']", "", "locale3;locale1", 0 },
      { "'~locale': ['locale1', 'locale2']", "", "locale2", 0 },
      { "'~locale': ['locale1', 'locale2']", "", "locale1;locale2", 0 },
      { "'~locale': ['locale1', 'locale2']", "", "locale3", 1 },
      { "'~locale': ['locale1', 'locale2']", "", "locale3;locale4", 1 },
    };
  gsize i;

  for (i = 0; i < G_N_ELEMENTS (vectors); i++)
    {
      g_autoptr(GPtrArray) actions = NULL;
      g_autoptr(GError) error = NULL;
      g_autofree gchar *formatted_data = NULL;

      g_test_message ("Vector %" G_GSIZE_FORMAT ": %s, %s, %s", i,
                      vectors[i].filters, vectors[i].env_arch, vectors[i].env_locales);
      g_setenv ("EOS_UPDATER_TEST_OVERRIDE_ARCHITECTURE", vectors[i].env_arch, TRUE);
      g_setenv ("EOS_UPDATER_TEST_UPDATER_OVERRIDE_LOCALES", vectors[i].env_locales, TRUE);

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wpragmas"
#pragma GCC diagnostic ignored "-Wsuggest-attribute=format"
#pragma GCC diagnostic ignored "-Wformat-nonliteral"
      formatted_data = g_strdup_printf (data, vectors[i].filters);
#pragma GCC diagnostic pop

      g_test_message ("%s", formatted_data);

      actions = euu_flatpak_ref_actions_from_data (formatted_data, -1, "test",
                                                   NULL, &error);

      g_assert_no_error (error);
      g_assert_nonnull (actions);
      g_assert_cmpuint (actions->len, ==, vectors[i].expected_n_actions);
    }

  if (old_env_arch != NULL)
    g_setenv ("EOS_UPDATER_TEST_OVERRIDE_ARCHITECTURE", old_env_arch, TRUE);
  else
    g_unsetenv ("EOS_UPDATER_TEST_OVERRIDE_ARCHITECTURE");
  if (old_env_locales != NULL)
    g_setenv ("EOS_UPDATER_TEST_UPDATER_OVERRIDE_LOCALES", old_env_locales, TRUE);
  else
    g_unsetenv ("EOS_UPDATER_TEST_UPDATER_OVERRIDE_LOCALES");
}

int
main (int   argc,
      char *argv[])
{
  setlocale (LC_ALL, "");

  g_test_init (&argc, &argv, NULL);

  g_test_add_func ("/flatpak/compress/install-update-as-install",
              test_compress_install_update_as_install);
  g_test_add_func ("/flatpak/compress/uninstall-update-as-uninstall",
              test_compress_uninstall_update_as_uninstall);
  g_test_add_func ("/flatpak/compress/install-uninstall-as-uninstall",
              test_compress_install_uninstall_as_uninstall);
  g_test_add_func ("/flatpak/compress/install-uninstall-install-as-install",
              test_compress_install_uninstall_install_as_install);
  g_test_add_func ("/flatpak/compress/update-update-as-update",
              test_compress_update_update_as_update);
  g_test_add_func ("/flatpak/compress/install-install-as-install",
              test_compress_install_install_as_install);
  g_test_add_func ("/flatpak/parse-autoinstall-file",
                   test_parse_autoinstall_file);
  g_test_add_func ("/flatpak/autoinstall-file-filters",
                   test_autoinstall_file_filters);

  return g_test_run ();
}