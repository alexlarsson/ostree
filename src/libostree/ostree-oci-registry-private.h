/*
 * Copyright © 2016 Red Hat, Inc
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.	 See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library. If not, see <http://www.gnu.org/licenses/>.
 *
 * Authors:
 *       Alexander Larsson <alexl@redhat.com>
 */

#pragma once

#include "libglnx/libglnx.h"

#include <glib.h>
#include <gio/gio.h>
#include <json-glib/json-glib.h>
#include "ostree-json-oci-private.h"

#define OSTREE_TYPE_OCI_REGISTRY ostree_oci_registry_get_type ()
#define OSTREE_OCI_REGISTRY(obj) (G_TYPE_CHECK_INSTANCE_CAST ((obj), OSTREE_TYPE_OCI_REGISTRY, OstreeOciRegistry))
#define OSTREE_IS_OCI_REGISTRY(obj) (G_TYPE_CHECK_INSTANCE_TYPE ((obj), OSTREE_TYPE_OCI_REGISTRY))

GType ostree_oci_registry_get_type (void);

typedef struct OstreeOciRegistry OstreeOciRegistry;

G_DEFINE_AUTOPTR_CLEANUP_FUNC (OstreeOciRegistry, g_object_unref)

OstreeOciRegistry  *ostree_oci_registry_new                  (const char           *uri,
                                                              gboolean              for_write,
                                                              int                   tmp_dfd,
                                                              GCancellable         *cancellable,
                                                              GError              **error);
gboolean            ostree_oci_registry_ensure_valid         (OstreeOciRegistry    *self,
                                                              gboolean              for_write,
                                                              GCancellable         *cancellable,
                                                              GError              **error);
OstreeOciRef       *ostree_oci_registry_load_ref             (OstreeOciRegistry    *self,
                                                              const char           *ref,
                                                              GCancellable         *cancellable,
                                                              GError              **error);
gboolean            ostree_oci_registry_set_ref              (OstreeOciRegistry    *self,
                                                              const char           *ref,
                                                              OstreeOciRef         *data,
                                                              GCancellable         *cancellable,
                                                              GError              **error);
void                ostree_oci_registry_download_blob        (OstreeOciRegistry    *self,
                                                              const char           *digest,
                                                              GCancellable         *cancellable,
                                                              GAsyncReadyCallback   callback,
                                                              gpointer              user_data);
int                 ostree_oci_registry_download_blob_finish (OstreeOciRegistry    *self,
                                                              GAsyncResult         *result,
                                                              GError              **error);
GBytes             *ostree_oci_registry_load_blob            (OstreeOciRegistry    *self,
                                                              const char           *digest,
                                                              GCancellable         *cancellable,
                                                              GError              **error);
char *              ostree_oci_registry_store_blob           (OstreeOciRegistry    *self,
                                                              GBytes               *data,
                                                              GCancellable         *cancellable,
                                                              GError              **error);
OstreeOciVersioned *ostree_oci_registry_load_versioned       (OstreeOciRegistry    *self,
                                                              const char           *digest,
                                                              GCancellable         *cancellable,
                                                              GError              **error);
