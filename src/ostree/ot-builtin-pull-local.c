/*
 * Copyright (C) 2011 Colin Walters <walters@verbum.org>
 *
 * SPDX-License-Identifier: LGPL-2.0+
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 *
 * Author: Colin Walters <walters@verbum.org>
 */

#include "config.h"

#include <unistd.h>
#include <stdlib.h>

#include "ot-main.h"
#include "ot-builtins.h"
#include "ostree.h"
#include "otutil.h"

static char *opt_remote;
static char *opt_collection_id;
static gboolean opt_commit_only;
static gboolean opt_disable_fsync;
static gboolean opt_untrusted;
static gboolean opt_bareuseronly_files;
static gboolean opt_require_static_deltas;
static gboolean opt_gpg_verify;
static gboolean opt_gpg_verify_summary;
static int opt_depth = 0;

/* ATTENTION:
 * Please remember to update the bash-completion script (bash/ostree) and
 * man page (man/ostree-pull-local.xml) when changing the option list.
 */

static GOptionEntry options[] = {
  { "commit-metadata-only", 0, 0, G_OPTION_ARG_NONE, &opt_commit_only, "Fetch only the commit metadata", NULL },
  { "remote", 0, 0, G_OPTION_ARG_STRING, &opt_remote, "Add REMOTE to refspec", "REMOTE" },
  { "disable-fsync", 0, 0, G_OPTION_ARG_NONE, &opt_disable_fsync, "Do not invoke fsync()", NULL },
  { "untrusted", 0, 0, G_OPTION_ARG_NONE, &opt_untrusted, "Verify checksums of local sources (always enabled for HTTP pulls)", NULL },
  { "bareuseronly-files", 0, 0, G_OPTION_ARG_NONE, &opt_bareuseronly_files, "Reject regular files with mode outside of 0775 (world writable, suid, etc.)", NULL },
  { "require-static-deltas", 0, 0, G_OPTION_ARG_NONE, &opt_require_static_deltas, "Require static deltas", NULL },
  { "gpg-verify", 0, 0, G_OPTION_ARG_NONE, &opt_gpg_verify, "GPG verify commits (must specify --remote)", NULL },
  { "gpg-verify-summary", 0, 0, G_OPTION_ARG_NONE, &opt_gpg_verify_summary, "GPG verify summary (must specify --remote)", NULL },
  { "depth", 0, 0, G_OPTION_ARG_INT, &opt_depth, "Traverse DEPTH parents (-1=infinite) (default: 0)", "DEPTH" },
  { "collection-id", 0, 0, G_OPTION_ARG_STRING, &opt_collection_id, "Pull from this collection ID", "COLLECTION-ID" },
  { NULL }
};

/* See canonical version of this in ot-builtin-pull.c */
static void
noninteractive_console_progress_changed (OstreeAsyncProgress *progress,
                                         gpointer             user_data)
{
  /* We do nothing here - we just want the final status */
}

gboolean
ostree_builtin_pull_local (int argc, char **argv, OstreeCommandInvocation *invocation, GCancellable *cancellable, GError **error)
{
  gboolean ret = FALSE;
  g_autoptr(GOptionContext) context = NULL;
  g_autoptr(OstreeRepo) repo = NULL;
  int i;
  const char *src_repo_arg;
  g_autofree char *src_repo_uri = NULL;
  g_autoptr(OstreeAsyncProgress) progress = NULL;
  g_autoptr(GPtrArray) refs_to_fetch = NULL;
  OstreeRepoPullFlags pullflags = 0;

  context = g_option_context_new ("SRC_REPO [REFS...]");

  if (!ostree_option_context_parse (context, options, &argc, &argv, invocation, &repo, cancellable, error))
    goto out;

  if (!ostree_ensure_repo_writable (repo, error))
    goto out;

  if (argc < 2)
    {
      gchar *help = g_option_context_get_help (context, TRUE, NULL);
      g_printerr ("%s\n", help);
      g_free (help);
      g_set_error_literal (error, G_IO_ERROR, G_IO_ERROR_FAILED,
                               "DESTINATION must be specified");
      goto out;
    }

  src_repo_arg = argv[1];

  if (src_repo_arg[0] == '/')
    src_repo_uri = g_strconcat ("file://", src_repo_arg, NULL);
  else
    { 
      g_autofree char *cwd = g_get_current_dir ();
      src_repo_uri = g_strconcat ("file://", cwd, "/", src_repo_arg, NULL);
    }

  if (opt_untrusted)
    pullflags |= OSTREE_REPO_PULL_FLAGS_UNTRUSTED;
  if (opt_bareuseronly_files)
    pullflags |= OSTREE_REPO_PULL_FLAGS_BAREUSERONLY_FILES;
  if (opt_commit_only)
    pullflags |= OSTREE_REPO_PULL_FLAGS_COMMIT_ONLY;

  if (opt_disable_fsync)
    ostree_repo_set_disable_fsync (repo, TRUE);

  if (argc == 2)
    {
      g_autoptr(GFile) src_repo_path = g_file_new_for_path (src_repo_arg);
      g_autoptr(OstreeRepo) src_repo = ostree_repo_new (src_repo_path);
      g_autoptr(GHashTable) refs_to_clone = NULL;

      refs_to_fetch = g_ptr_array_new_with_free_func (g_free);

      if (!ostree_repo_open (src_repo, cancellable, error))
        goto out;

      if (opt_collection_id)
        {
          if (!ostree_repo_list_collection_refs (src_repo, opt_collection_id,
                                                 &refs_to_clone,
                                                 OSTREE_REPO_LIST_REFS_EXT_NONE,
                                                 cancellable, error))
            goto out;
        }
      else
        {
          if (!ostree_repo_list_refs (src_repo, NULL, &refs_to_clone,
                                      cancellable, error))
            goto out;
        }

      { GHashTableIter hashiter;
        gpointer hkey, hvalue;

        g_hash_table_iter_init (&hashiter, refs_to_clone);
        while (g_hash_table_iter_next (&hashiter, &hkey, &hvalue))
          {
            if (opt_collection_id)
              {
                OstreeCollectionRef *key = hkey;
                g_ptr_array_add (refs_to_fetch, g_strdup (key->ref_name));
              }
            else
              g_ptr_array_add (refs_to_fetch, g_strdup (hkey));
          }
      }
    }
  else
    {
      refs_to_fetch = g_ptr_array_new ();
      for (i = 2; i < argc; i++)
        {
          const char *ref = argv[i];
          
          g_ptr_array_add (refs_to_fetch, (char*)ref);
        }
    }

  { GVariantBuilder builder;
    g_autoptr(GVariant) opts = NULL;
    g_auto(GLnxConsoleRef) console = { 0, };

    glnx_console_lock (&console);

    g_variant_builder_init (&builder, G_VARIANT_TYPE ("a{sv}"));

    g_variant_builder_add (&builder, "{s@v}", "flags",
                           g_variant_new_variant (g_variant_new_int32 (pullflags)));
    if (opt_collection_id)
      {
        GVariantBuilder refs_builder;
        g_variant_builder_init (&refs_builder, G_VARIANT_TYPE ("a(sss)"));

        for (i = 0; i < refs_to_fetch->len; i++)
          g_variant_builder_add (&refs_builder, "(sss)",
                                 opt_collection_id, g_ptr_array_index (refs_to_fetch, i), "");

        g_variant_builder_add (&builder, "{s@v}", "collection-refs",
                               g_variant_new_variant (g_variant_builder_end (&refs_builder)));
      }
    else
      g_variant_builder_add (&builder, "{s@v}", "refs",
                             g_variant_new_variant (g_variant_new_strv ((const char *const*) refs_to_fetch->pdata, refs_to_fetch->len)));
    if (opt_remote)
      g_variant_builder_add (&builder, "{s@v}", "override-remote-name",
                             g_variant_new_variant (g_variant_new_string (opt_remote)));
    g_variant_builder_add (&builder, "{s@v}", "require-static-deltas",
                           g_variant_new_variant (g_variant_new_boolean (opt_require_static_deltas)));
    if (opt_gpg_verify)
      g_variant_builder_add (&builder, "{s@v}", "gpg-verify",
                             g_variant_new_variant (g_variant_new_boolean (TRUE)));
    if (opt_gpg_verify_summary)
      g_variant_builder_add (&builder, "{s@v}", "gpg-verify-summary",
                             g_variant_new_variant (g_variant_new_boolean (TRUE)));
    g_variant_builder_add (&builder, "{s@v}", "depth",
                           g_variant_new_variant (g_variant_new_int32 (opt_depth)));

    if (console.is_tty)
      progress = ostree_async_progress_new_and_connect (ostree_repo_pull_default_console_progress_changed, &console);
    else
      progress = ostree_async_progress_new_and_connect (noninteractive_console_progress_changed, &console);

    opts = g_variant_ref_sink (g_variant_builder_end (&builder));
    if (!ostree_repo_pull_with_options (repo, src_repo_uri, 
                                        opts,
                                        progress,
                                        cancellable, error))
      goto out;

    if (!console.is_tty)
      {
        g_assert (progress);
        const char *status = ostree_async_progress_get_status (progress);
        if (status)
          g_print ("%s\n", status);
      }
    ostree_async_progress_finish (progress);
  }

  ret = TRUE;
 out:
  if (repo)
    ostree_repo_abort_transaction (repo, cancellable, NULL);
  return ret;
}
