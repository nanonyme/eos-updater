/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*-
 *
 * Copyright © 2022 Endless OS Foundation, LLC
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
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
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
 * 02110-1301 USA
 */

#include "config.h"

#include <test-common/gpg.h>
#include <test-common/utils.h>

#include <glib.h>
#include <locale.h>

static void
setup_basic_test_server_client (EosUpdaterFixture  *fixture,
                                EosTestServer     **out_server,
                                EosTestSubserver  **out_subserver,
                                EosTestClient     **out_client)
{
  g_autoptr(GError) error = NULL;
  g_autofree gchar *keyid = get_keyid (fixture->gpg_home);
  g_autoptr(GFile) server_root = NULL;
  g_autoptr(EosTestServer) server = NULL;
  g_autoptr(EosTestSubserver) subserver = NULL;
  g_autoptr(GFile) client_root = NULL;
  g_autoptr(EosTestClient) client = NULL;
  g_autoptr(GHashTable) additional_metadata_for_commit = NULL;

  /* Arbitrarily say that the currently booted commit is version 1.0.0. */
  eos_test_add_metadata_for_commit (&additional_metadata_for_commit,
                                    0, "version", "1.0.0");

  server_root = g_file_get_child (fixture->tmpdir, "main");
  server = eos_test_server_new_quick (server_root,
                                      default_vendor,
                                      default_product,
                                      default_collection_ref,
                                      0,
                                      fixture->gpg_home,
                                      keyid,
                                      default_ostree_path,
                                      NULL, NULL,
                                      additional_metadata_for_commit,
                                      &error);
  g_assert_no_error (error);
  g_assert_nonnull (server);

  g_assert_cmpuint (server->subservers->len, ==, 1u);

  subserver = g_object_ref (EOS_TEST_SUBSERVER (g_ptr_array_index (server->subservers, 0)));
  client_root = g_file_get_child (fixture->tmpdir, "client");
  client = eos_test_client_new (client_root,
                                default_remote_name,
                                subserver,
                                default_collection_ref,
                                default_vendor,
                                default_product,
                                default_auto_bootloader,
                                &error);
  g_assert_no_error (error);
  g_assert_nonnull (client);

  *out_server = g_steal_pointer (&server);
  *out_subserver = g_steal_pointer (&subserver);
  *out_client = g_steal_pointer (&client);
}

static void
get_poll_results (GFile    *autoupdater_dir,
                  guint64  *last_changed_usecs,
                  gchar   **update_refspec,
                  gchar   **update_id)
{
  g_autoptr(GFile) state_dir = g_file_get_child (autoupdater_dir, "state");
  g_autoptr(GFile) results_file = g_file_get_child (state_dir, "autoupdater-poll-results");
  g_autoptr(GBytes) results_bytes = NULL;
  g_autoptr(GError) error = NULL;
  g_autoptr(GVariant) results_variant = NULL;

  results_bytes = g_file_load_bytes (results_file, NULL, NULL, &error);
  g_assert_no_error (error);
  g_assert_nonnull (results_bytes);

  results_variant = g_variant_new_from_bytes (G_VARIANT_TYPE ("a{sv}"),
                                              results_bytes,
                                              FALSE);
  if (last_changed_usecs &&
      !g_variant_lookup (results_variant, "LastChangedUsecs", "t", last_changed_usecs))
    *last_changed_usecs = 0;
  if (update_refspec &&
      !g_variant_lookup (results_variant, "UpdateRefspec", "s", update_refspec))
    *update_refspec = g_strdup ("");
  if (update_id &&
      !g_variant_lookup (results_variant, "UpdateID", "s", update_id))
    *update_id = g_strdup ("");
}

static void
test_poll_results (EosUpdaterFixture *fixture,
                   gconstpointer      user_data)
{
  g_autoptr(GError) error = NULL;
  g_autoptr(EosTestServer) server = NULL;
  g_autoptr(EosTestSubserver) subserver = NULL;
  g_autoptr(EosTestClient) client = NULL;
  g_autoptr(GHashTable) leaf_commit_nodes =
    eos_test_subserver_ref_to_commit_new ();
  DownloadSource main_source = DOWNLOAD_MAIN;
  g_auto(CmdAsyncResult) updater_cmd = CMD_ASYNC_RESULT_CLEARED;
  g_autoptr(GFile) autoupdater_root = g_file_get_child (fixture->tmpdir, "autoupdater");
  g_autoptr(EosTestAutoupdater) autoupdater = NULL;
  g_auto(CmdResult) reaped = CMD_RESULT_CLEARED;
  guint64 last_changed_usecs = 0;
  g_autofree gchar *update_refspec = NULL;
  g_autofree gchar *update_id = NULL;
  guint64 prev_last_changed_usecs;
  const gchar *expected_update_id;

  if (eos_test_skip_chroot ())
    return;

  setup_basic_test_server_client (fixture, &server, &subserver, &client);

  eos_test_client_run_updater (client,
                               &main_source,
                               1,
                               NULL,
                               &updater_cmd,
                               &error);
  g_assert_no_error (error);

  /* First poll with no update available. */
  autoupdater = eos_test_autoupdater_new (autoupdater_root,
                                          UPDATE_STEP_POLL,
                                          0,  /* interval (days) */
                                          0, /* user visible delay (days) */
                                          FALSE,  /* force update */
                                          &error);
  g_assert_no_error (error);
  g_assert_true (cmd_result_ensure_ok_verbose (autoupdater->cmd));

  get_poll_results (autoupdater_root,
                    &last_changed_usecs,
                    &update_refspec,
                    &update_id);
  g_assert_cmpuint (last_changed_usecs, >, 0);
  g_assert_cmpstr (update_refspec, ==, "");
  g_assert_cmpstr (update_id, ==, "");

  /* Make a commit and check that the results were updated. */
  g_hash_table_insert (leaf_commit_nodes,
                       ostree_collection_ref_dup (default_collection_ref),
                       GUINT_TO_POINTER (1));
  eos_test_subserver_populate_commit_graph_from_leaf_nodes (subserver,
                                                            leaf_commit_nodes);
  eos_test_subserver_update (subserver, &error);
  g_assert_no_error (error);

  g_clear_object (&autoupdater);
  prev_last_changed_usecs = last_changed_usecs;
  last_changed_usecs = 0;
  g_clear_pointer (&update_refspec, g_free);
  g_clear_pointer (&update_id, g_free);
  g_usleep (1);
  autoupdater = eos_test_autoupdater_new (autoupdater_root,
                                          UPDATE_STEP_POLL,
                                          0,  /* interval (days) */
                                          0, /* user visible delay (days) */
                                          FALSE,  /* force update */
                                          &error);
  g_assert_no_error (error);
  g_assert_true (cmd_result_ensure_ok_verbose (autoupdater->cmd));

  expected_update_id = g_hash_table_lookup (subserver->commits_in_repo,
                                            GUINT_TO_POINTER (1));
  g_assert_nonnull (expected_update_id);
  get_poll_results (autoupdater_root,
                    &last_changed_usecs,
                    &update_refspec,
                    &update_id);
  g_assert_cmpuint (last_changed_usecs, >, prev_last_changed_usecs);
  g_assert_cmpstr (update_refspec, ==, "REMOTE:REF");
  g_assert_cmpstr (update_id, ==, expected_update_id);

  /* Run the autoupdater again and check that the results haven't changed. */
  g_clear_object (&autoupdater);
  prev_last_changed_usecs = last_changed_usecs;
  last_changed_usecs = 0;
  g_clear_pointer (&update_refspec, g_free);
  g_clear_pointer (&update_id, g_free);
  g_usleep (1);
  autoupdater = eos_test_autoupdater_new (autoupdater_root,
                                          UPDATE_STEP_POLL,
                                          0,  /* interval (days) */
                                          0, /* user visible delay (days) */
                                          FALSE,  /* force update */
                                          &error);
  g_assert_no_error (error);
  g_assert_true (cmd_result_ensure_ok_verbose (autoupdater->cmd));

  get_poll_results (autoupdater_root,
                    &last_changed_usecs,
                    &update_refspec,
                    &update_id);
  g_assert_cmpuint (last_changed_usecs, ==, prev_last_changed_usecs);
  g_assert_cmpstr (update_refspec, ==, "REMOTE:REF");
  g_assert_cmpstr (update_id, ==, expected_update_id);

  eos_test_client_reap_updater (client,
                                &updater_cmd,
                                &reaped,
                                &error);
  g_assert_no_error (error);
  g_assert_true (cmd_result_ensure_ok_verbose (&reaped));
}

typedef struct {
  guint    update_delay;
  gboolean force_update;
  gboolean expected_has_commit;
} UserVisibleTestData;

static void
test_user_visible_update_delay (EosUpdaterFixture *fixture,
                                gconstpointer      user_data)
{
  const UserVisibleTestData *test_data = user_data;
  g_autoptr(GError) error = NULL;
  g_autoptr(EosTestServer) server = NULL;
  g_autoptr(EosTestSubserver) subserver = NULL;
  g_autoptr(EosTestClient) client = NULL;
  g_autoptr(GHashTable) leaf_commit_nodes =
    eos_test_subserver_ref_to_commit_new ();
  DownloadSource main_source = DOWNLOAD_MAIN;
  g_auto(CmdAsyncResult) updater_cmd = CMD_ASYNC_RESULT_CLEARED;
  g_autoptr(GFile) autoupdater_root = g_file_get_child (fixture->tmpdir, "autoupdater");
  g_autoptr(EosTestAutoupdater) autoupdater = NULL;
  g_auto(CmdResult) reaped = CMD_RESULT_CLEARED;
  gboolean has_commit = FALSE;

  if (eos_test_skip_chroot ())
    return;

  g_assert_nonnull (test_data);
  g_test_message ("User visible test data %p: "
                  "update_delay=%u, "
                  "force_update=%s, "
                  "expected_has_commit=%s",
                  test_data,
                  test_data->update_delay,
                  test_data->force_update ? "TRUE" : "FALSE",
                  test_data->expected_has_commit ? "TRUE" : "FALSE");

  setup_basic_test_server_client (fixture, &server, &subserver, &client);

  /* Make a user visible update commit. */
  g_hash_table_insert (leaf_commit_nodes,
                       ostree_collection_ref_dup (default_collection_ref),
                       GUINT_TO_POINTER (1));
  eos_test_add_metadata_for_commit (&subserver->additional_metadata_for_commit,
                                    1, "version", "2.0.0");
  eos_test_subserver_populate_commit_graph_from_leaf_nodes (subserver,
                                                            leaf_commit_nodes);
  eos_test_subserver_update (subserver, &error);
  g_assert_no_error (error);

  eos_test_client_run_updater (client,
                               &main_source,
                               1,
                               NULL,
                               &updater_cmd,
                               &error);
  g_assert_no_error (error);

  /* First poll so that poll results are written to disk. */
  g_clear_object (&autoupdater);
  autoupdater = eos_test_autoupdater_new (autoupdater_root,
                                          UPDATE_STEP_POLL,
                                          0,  /* interval (days) */
                                          test_data->update_delay, /* user visible delay (days) */
                                          test_data->force_update,  /* force update */
                                          &error);
  g_assert_no_error (error);
  g_assert_true (cmd_result_ensure_ok_verbose (autoupdater->cmd));

  /* Now run through to apply. Since eos-updater is in UPDATE_AVAILABLE state,
   * polling will be skipped. This will test that the autoupdater loads the
   * previous poll results even when not polling. */
  g_clear_object (&autoupdater);
  autoupdater = eos_test_autoupdater_new (autoupdater_root,
                                          UPDATE_STEP_APPLY,
                                          0,  /* interval (days) */
                                          test_data->update_delay, /* user visible delay (days) */
                                          test_data->force_update,  /* force update */
                                          &error);
  g_assert_no_error (error);
  g_assert_true (cmd_result_ensure_ok_verbose (autoupdater->cmd));

  eos_test_client_has_commit (client,
                              default_remote_name,
                              1,
                              &has_commit,
                              &error);
  g_assert_no_error (error);
  if (test_data->expected_has_commit)
    g_assert_true (has_commit);
  else
    g_assert_false (has_commit);

  eos_test_client_reap_updater (client,
                                &updater_cmd,
                                &reaped,
                                &error);
  g_assert_no_error (error);
  g_assert_true (cmd_result_ensure_ok_verbose (&reaped));
}

int
main (int    argc,
      char **argv)
{
  setlocale (LC_ALL, "");

  g_test_init (&argc, &argv, G_TEST_OPTION_ISOLATE_DIRS, NULL);

  eos_test_add ("/autoupdater/poll-results", NULL, test_poll_results);
  eos_test_add ("/autoupdater/user-visible-update/delay",
                (&(UserVisibleTestData) {
                  .update_delay = 1,
                  .force_update = FALSE,
                  .expected_has_commit = FALSE,
                }),
                test_user_visible_update_delay);
  eos_test_add ("/autoupdater/user-visible-update/nodelay",
                (&(UserVisibleTestData) {
                  .update_delay = 0,
                  .force_update = FALSE,
                  .expected_has_commit = TRUE,
                }),
                test_user_visible_update_delay);
  eos_test_add ("/autoupdater/user-visible-update/force",
                (&(UserVisibleTestData) {
                  .update_delay = 1,
                  .force_update = TRUE,
                  .expected_has_commit = TRUE,
                }),
                test_user_visible_update_delay);

  return g_test_run ();
}
