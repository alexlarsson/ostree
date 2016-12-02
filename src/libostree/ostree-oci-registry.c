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

#include "config.h"

#include <glib/gi18n.h>

#include "libglnx.h"

#include <libsoup/soup.h>
#include "ostree-oci-registry-private.h"
#include "ostree-json-oci-private.h"
#include "ostree-fetcher.h"
#include "otutil.h"

/* We don't get these due to defining SOUP_VERSION_MAX_ALLOWED, so we
   define our own versions: */
G_DEFINE_AUTOPTR_CLEANUP_FUNC (SoupSession, g_object_unref)
G_DEFINE_AUTOPTR_CLEANUP_FUNC (SoupMessage, g_object_unref)
G_DEFINE_AUTOPTR_CLEANUP_FUNC (SoupRequest, g_object_unref)
G_DEFINE_AUTOPTR_CLEANUP_FUNC (SoupRequestHTTP, g_object_unref)
G_DEFINE_AUTOPTR_CLEANUP_FUNC (SoupURI, soup_uri_free)

#define MAX_JSON_SIZE (1024 * 1024)

static void ostree_oci_registry_initable_iface_init (GInitableIface *iface);

struct OstreeOciRegistry
{
  GObject parent;

  gboolean for_write;
  gboolean valid;
  char *uri;
  int tmp_dfd;

  /* Local repos */
  int dfd;

  /* Remote repos */
  OstreeFetcher *fetcher;
  SoupURI *base_uri;
};

typedef struct
{
  GObjectClass parent_class;
} OstreeOciRegistryClass;

enum {
  PROP_0,

  PROP_URI,
  PROP_FOR_WRITE,
  PROP_TMP_DFD,
};

G_DEFINE_TYPE_WITH_CODE (OstreeOciRegistry, ostree_oci_registry, G_TYPE_OBJECT,
                         G_IMPLEMENT_INTERFACE (G_TYPE_INITABLE,
                                                ostree_oci_registry_initable_iface_init))

static void
ostree_oci_registry_finalize (GObject *object)
{
  OstreeOciRegistry *self = OSTREE_OCI_REGISTRY (object);

  if (self->dfd != -1)
    close (self->dfd);

  g_clear_pointer (&self->base_uri, soup_uri_free);
  g_clear_object (&self->fetcher);
  g_free (self->uri);

  G_OBJECT_CLASS (ostree_oci_registry_parent_class)->finalize (object);
}

static void
ostree_oci_registry_set_property (GObject         *object,
                                  guint            prop_id,
                                  const GValue    *value,
                                  GParamSpec      *pspec)
{
  OstreeOciRegistry *self = OSTREE_OCI_REGISTRY (object);
  const char *uri;

  switch (prop_id)
    {
    case PROP_URI:
      /* Ensure the base uri ends with a / so relative urls work */
      uri = g_value_get_string (value);
      if (g_str_has_prefix (uri, "/"))
        self->uri = g_strdup (uri);
      else
        self->uri = g_strconcat (uri, "/", NULL);
      break;
    case PROP_FOR_WRITE:
      self->for_write = g_value_get_boolean (value);
      break;
    case PROP_TMP_DFD:
      self->tmp_dfd = g_value_get_int (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    }
}

static void
ostree_oci_registry_get_property (GObject         *object,
                                  guint            prop_id,
                                  GValue          *value,
                                  GParamSpec      *pspec)
{
  OstreeOciRegistry *self = OSTREE_OCI_REGISTRY (object);

  switch (prop_id)
    {
    case PROP_URI:
      g_value_set_string (value, self->uri);
      break;
    case PROP_FOR_WRITE:
      g_value_set_boolean (value, self->for_write);
      break;
    case PROP_TMP_DFD:
      g_value_set_int (value, self->tmp_dfd);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    }
}

static void
ostree_oci_registry_class_init (OstreeOciRegistryClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->finalize = ostree_oci_registry_finalize;
  object_class->get_property = ostree_oci_registry_get_property;
  object_class->set_property = ostree_oci_registry_set_property;

  g_object_class_install_property (object_class,
                                   PROP_URI,
                                   g_param_spec_string ("uri",
                                                        "",
                                                        "",
                                                        NULL,
                                                        G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));
  g_object_class_install_property (object_class,
                                   PROP_TMP_DFD,
                                   g_param_spec_int ("tmp-dfd",
                                                     "",
                                                     "",
                                                     -1, G_MAXINT, -1,
                                                     G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));
  g_object_class_install_property (object_class,
                                   PROP_FOR_WRITE,
                                   g_param_spec_boolean ("for-write",
                                                         "",
                                                         "",
                                                         FALSE,
                                                         G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));
}

static void
ostree_oci_registry_init (OstreeOciRegistry *self)
{
  self->dfd = -1;
  self->tmp_dfd = -1;
}

OstreeOciRegistry *
ostree_oci_registry_new (const char *uri,
                         gboolean for_write,
                         int tmp_dfd,
                         GCancellable *cancellable,
                         GError **error)
{
  OstreeOciRegistry *oci_registry;

  oci_registry = g_initable_new (OSTREE_TYPE_OCI_REGISTRY,
                                 cancellable, error,
                                 "uri", uri,
                                 "for-write", for_write,
                                 "tmp-dfd", tmp_dfd,
                                 NULL);

  return oci_registry;
}

static int
local_open_file (int dfd,
                 const char *subpath,
                 GCancellable *cancellable,
                 GError **error)
{
  glnx_fd_close int fd = -1;

  do
    fd = openat (dfd, subpath, O_RDONLY | O_NONBLOCK | O_CLOEXEC | O_NOCTTY);
  while (G_UNLIKELY (fd == -1 && errno == EINTR));
  if (fd == -1)
    {
      glnx_set_error_from_errno (error);
      return -1;
    }

  return glnx_steal_fd (&fd);
}

static GBytes *
local_load_file (int dfd,
                 const char *subpath,
                 GCancellable *cancellable,
                 GError **error)
{
  glnx_fd_close int fd = -1;

  fd = local_open_file (dfd, subpath, cancellable, error);
  if (fd == -1)
    return NULL;

  return glnx_fd_readall_bytes (fd, cancellable, error);
}

static GBytes *
remote_load_file (OstreeFetcher *fetcher,
                  SoupURI *base,
                  const char *subpath,
                  GCancellable *cancellable,
                  GError **error)
{
  g_autoptr(SoupURI) uri = NULL;
  g_autoptr(GBytes) bytes = NULL;

  uri = soup_uri_new_with_base (base, subpath);
  if (uri == NULL)
    {
      g_set_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT,
                   "Invalid relative url %s", subpath);
      return NULL;
    }

  if (!_ostree_fetcher_request_uri_to_membuf (fetcher, uri, FALSE, FALSE,
                                              &bytes, MAX_JSON_SIZE,
                                              cancellable, error))
    return NULL;

  return g_steal_pointer (&bytes);
}

static GBytes *
ostree_oci_registry_load_file (OstreeOciRegistry  *self,
                               const char *subpath,
                               GCancellable *cancellable,
                               GError **error)
{
  if (self->dfd != -1)
    return local_load_file (self->dfd, subpath, cancellable, error);
  else
    return remote_load_file (self->fetcher, self->base_uri, subpath, cancellable, error);
}

static JsonNode *
parse_json (GBytes *bytes, GCancellable *cancellable, GError **error)
{
  g_autoptr(JsonParser) parser = NULL;
  JsonNode *root = NULL;

  parser = json_parser_new ();
  if (!json_parser_load_from_data (parser,
                                   g_bytes_get_data (bytes, NULL),
                                   g_bytes_get_size (bytes),
                                   error))
    return NULL;

  root = json_parser_get_root (parser);
  if (root == NULL || !JSON_NODE_HOLDS_OBJECT (root))
    {
      g_set_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA, "Invalid json, no root object");
      return NULL;
    }

  return json_node_ref (root);
}

static JsonNode *
ostree_oci_registry_load_json (OstreeOciRegistry  *self,
                               const char *subpath,
                               GCancellable *cancellable,
                               GError **error)
{
  g_autoptr(GBytes) bytes = NULL;

  bytes = ostree_oci_registry_load_file (self, subpath, cancellable, error);
  if (bytes == NULL)
    return NULL;

  return parse_json (bytes, cancellable, error);
}

static gboolean
verify_oci_version (GBytes *oci_layout_bytes, GCancellable *cancellable, GError **error)
{
  const char *version;
  g_autoptr(JsonNode) node = NULL;
  JsonObject *oci_layout;

  node = parse_json (oci_layout_bytes, cancellable, error);
  if (node == NULL)
    return FALSE;

  oci_layout = json_node_get_object (node);

  version = json_object_get_string_member (oci_layout, "imageLayoutVersion");
  if (version == NULL)
    {
      g_set_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA, "Unsupported oci repo: oci-layout version missing");
      return FALSE;
    }

  if (strcmp (version, "1.0.0") != 0)
    {
      g_set_error (error, G_IO_ERROR, G_IO_ERROR_NOT_SUPPORTED,
                   "Unsupported existing oci-layout version %s (only 1.0.0 supported)", version);
      return FALSE;
    }

  return TRUE;
}

static gboolean
ostree_oci_registry_ensure_local (OstreeOciRegistry *self,
                                  gboolean for_write,
                                  GCancellable *cancellable,
                                  GError **error)
{
  g_autoptr(GFile) dir = g_file_new_for_uri (self->uri);
  glnx_fd_close int local_dfd = -1;
  int dfd;
  g_autoptr(GError) local_error = NULL;
  g_autoptr(GBytes) oci_layout_bytes = NULL;

  if (self->dfd != -1)
    dfd = self->dfd;
  else
    {
      if (!glnx_opendirat (AT_FDCWD, ot_file_get_path_cached (dir),
                           TRUE, &local_dfd, &local_error))
        {
          if (for_write && g_error_matches (local_error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND))
            {
              g_clear_error (&local_error);

              if (!glnx_shutil_mkdir_p_at (AT_FDCWD, ot_file_get_path_cached (dir), 0755, cancellable, error))
                return FALSE;

              if (!glnx_opendirat (AT_FDCWD, ot_file_get_path_cached (dir),
                                   TRUE, &local_dfd, error))
                return FALSE;
            }
          else
            {
              g_propagate_error (error, g_steal_pointer (&local_error));
              return FALSE;
            }
        }

      dfd = local_dfd;
    }

  if (for_write)
    {
      if (!glnx_shutil_mkdir_p_at (dfd, "blobs/sha256", 0755, cancellable, error))
        return FALSE;

      if (!glnx_shutil_mkdir_p_at (dfd, "refs", 0755, cancellable, error))
        return FALSE;
    }

  oci_layout_bytes = local_load_file (dfd, "oci-layout", cancellable, &local_error);
  if (oci_layout_bytes == NULL)
    {
      if (for_write && g_error_matches (local_error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND))
        {
          const char *new_layout_data = "{\"imageLayoutVersion\": \"1.0.0\"}";

          g_clear_error (&local_error);

          if (!glnx_file_replace_contents_at (dfd, "oci-layout",
                                              (const guchar *)new_layout_data,
                                              strlen (new_layout_data),
                                              0,
                                              cancellable, error))
            return FALSE;
        }
      else
        {
          g_propagate_error (error, g_steal_pointer (&local_error));
          return FALSE;
        }
    }
  else if (!verify_oci_version (oci_layout_bytes, cancellable, error))
    return FALSE;

  if (self->dfd == -1 && local_dfd != -1)
    self->dfd = glnx_steal_fd (&local_dfd);

  return TRUE;
}

static gboolean
ostree_oci_registry_ensure_remote (OstreeOciRegistry *self,
                                   gboolean for_write,
                                   GCancellable *cancellable,
                                   GError **error)
{
  g_autoptr(OstreeFetcher) fetcher = NULL;
  g_autoptr(SoupURI) baseuri = NULL;
  g_autoptr(GBytes) oci_layout_bytes = NULL;

  if (for_write)
    {
      g_set_error (error, G_IO_ERROR, G_IO_ERROR_NOT_SUPPORTED,
                   "Writes are not supported for remote OCI registries");
      return FALSE;
    }

  baseuri = soup_uri_new (self->uri);
  if (baseuri == NULL)
    {
      g_set_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT,
                   "Invalid url %s", self->uri);
      return FALSE;
    }

  fetcher = _ostree_fetcher_new (self->tmp_dfd, 0);

  oci_layout_bytes = remote_load_file (fetcher, baseuri, "oci-layout", cancellable, error);
  if (oci_layout_bytes == NULL)
    return FALSE;

  if (!verify_oci_version (oci_layout_bytes, cancellable, error))
    return FALSE;

  self->base_uri = g_steal_pointer (&baseuri);
  self->fetcher = g_steal_pointer (&fetcher);

  return TRUE;
}

static gboolean
ostree_oci_registry_initable_init (GInitable     *initable,
                                   GCancellable  *cancellable,
                                   GError       **error)
{
  OstreeOciRegistry *self = OSTREE_OCI_REGISTRY (initable);
  glnx_fd_close int dfd = -1;
  g_autoptr(GFile) registry_blobs = NULL;
  g_autoptr(GFile) registry_blobs_sha256 = NULL;
  g_autoptr(GFile) registry_refs = NULL;
  g_autoptr(JsonObject) oci_layout = NULL;
  g_autoptr(GError) local_error = NULL;
  gboolean res;

  if (g_str_has_prefix (self->uri, "file:/"))
    res = ostree_oci_registry_ensure_local (self, self->for_write, cancellable, error);
  else
    res = ostree_oci_registry_ensure_remote (self, self->for_write, cancellable, error);

  if (!res)
    return FALSE;

  self->valid = TRUE;

  return TRUE;
}

static void
ostree_oci_registry_initable_iface_init (GInitableIface *iface)
{
  iface->init = ostree_oci_registry_initable_init;
}


OstreeOciRef *
ostree_oci_registry_load_ref (OstreeOciRegistry  *self,
                              const char         *ref,
                              GCancellable       *cancellable,
                              GError            **error)
{
  g_autoptr(JsonNode) node = NULL;
  g_autofree char *subpath = g_strdup_printf ("refs/%s", ref);
  g_autoptr(OstreeJson) descriptor = NULL;

  g_assert (self->valid);

  node = ostree_oci_registry_load_json (self, subpath, cancellable, error);
  if (node == NULL)
    return NULL;

  return (OstreeOciRef *)ostree_json_from_node (node, OSTREE_TYPE_OCI_REF, error);
}

gboolean
ostree_oci_registry_set_ref (OstreeOciRegistry  *self,
                             const char         *ref,
                             OstreeOciRef       *data,
                             GCancellable       *cancellable,
                             GError            **error)
{
  g_autoptr(GBytes) bytes = NULL;
  g_autofree char *subpath = g_strdup_printf ("refs/%s", ref);

  g_assert (self->valid);

  bytes = ostree_json_to_bytes (OSTREE_JSON (data));

  if (!glnx_file_replace_contents_at (self->dfd, subpath,
                                      g_bytes_get_data (bytes, NULL),
                                      g_bytes_get_size (bytes),
                                      0, cancellable, error))
    return FALSE;

  return TRUE;
}

typedef struct {
  OstreeOciRegistry *self;
  char *digest;
} DownloadData;

static void
download_data_free (DownloadData *data)
{
  g_object_unref (data->self);
  g_free (data->digest);
  g_free (data);
}

static void
download_blob_cb (GObject *source_object,
                  GAsyncResult *res,
                  gpointer user_data)
{
  g_autoptr(GTask) task = G_TASK (user_data);
  DownloadData *data = g_task_get_task_data (task);
  OstreeOciRegistry *self = data->self;
  g_autofree char *name = NULL;
  g_autofree char *checksum = NULL;
  GError *local_error = NULL;
  int fd;

  name = _ostree_fetcher_mirrored_request_with_partial_finish (OSTREE_FETCHER (source_object),
                                                               res, &local_error);
  if (name == NULL)
    {
      g_task_return_error (task, local_error);
      return;
    }

  fd = local_open_file (_ostree_fetcher_get_dfd (self->fetcher), name,
                        g_task_get_cancellable (task), &local_error);
  if (fd == -1)
    {
      g_task_return_error (task, local_error);
      return;
    }

  checksum = ot_checksum_file_at (_ostree_fetcher_get_dfd (self->fetcher), name, G_CHECKSUM_SHA256,
                                  g_task_get_cancellable (task), &local_error);

  (void) unlinkat (self->tmp_dfd, name, 0);

  if (checksum == NULL)
    {
      g_task_return_error (task, local_error);
      return;
    }

  if (strcmp (checksum, data->digest + strlen ("sha256:")) != 0)
    {
      g_task_return_new_error (task, G_IO_ERROR, G_IO_ERROR_FAILED,
                               "Checksum digest did not match (%s != %s)", data->digest, checksum);
      return;
    }

  g_task_return_int (task, fd);
}

void
ostree_oci_registry_download_blob (OstreeOciRegistry    *self,
                                   const char           *digest,
                                   GCancellable         *cancellable,
                                   GAsyncReadyCallback   callback,
                                   gpointer              user_data)
{
  g_autoptr(GTask) task = NULL;
  g_autofree char *subpath = NULL;
  g_autoptr(SoupURI) uri = NULL;
  g_autoptr(GPtrArray) mirrorlist = g_ptr_array_new ();
  DownloadData *data;

  g_assert (self->valid);

  task = g_task_new (self, cancellable, callback, user_data);

  data = g_new0 (DownloadData, 1);
  data->self = g_object_ref (self);
  data->digest = g_ascii_strdown (digest, -1);
  g_task_set_task_data (task, data, (GDestroyNotify) download_data_free);

  if (!g_str_has_prefix (digest, "sha256:"))
    {
      g_task_return_new_error (task, G_IO_ERROR, G_IO_ERROR_NOT_SUPPORTED,
                               "Unsupported digest type %s", digest);
      return;
    }

  subpath = g_strdup_printf ("blobs/sha256/%s", digest + strlen ("sha256:"));

  if (self->dfd != -1)
    {
      GError *local_error = NULL;
      int fd;

      /* Local case, trust checksum */

      fd = local_open_file (self->dfd, subpath, cancellable, &local_error);
      if (fd == -1)
        {
          g_task_return_error (task, local_error);
          return;
        }
      g_task_return_int (task, fd);
      return;
    }
  else
    {
      /* remote case, download and verify */

      g_ptr_array_add (mirrorlist, self->base_uri);
      _ostree_fetcher_mirrored_request_with_partial_async (self->fetcher,
                                                           mirrorlist,
                                                           subpath,
                                                           G_MAXINT64,
                                                           0,
                                                           cancellable,
                                                           download_blob_cb,
                                                           g_steal_pointer (&task));
    }
}

int
ostree_oci_registry_download_blob_finish (OstreeOciRegistry  *self,
                                          GAsyncResult       *result,
                                          GError            **error)
{
  return g_task_propagate_int (G_TASK (result), error);
}

GBytes *
ostree_oci_registry_load_blob (OstreeOciRegistry  *self,
                               const char         *digest,
                               GCancellable       *cancellable,
                               GError            **error)
{
  g_autofree char *subpath = NULL;

  g_assert (self->valid);

  if (!g_str_has_prefix (digest, "sha256:"))
    {
      g_set_error (error, G_IO_ERROR, G_IO_ERROR_NOT_SUPPORTED,
                   "Unsupported digest type %s", digest);
      return NULL;
    }

  subpath = g_strdup_printf ("blobs/sha256/%s", digest + strlen ("sha256:"));

  return ostree_oci_registry_load_file (self, subpath, cancellable, error);
}

char *
ostree_oci_registry_store_blob (OstreeOciRegistry  *self,
                                GBytes             *data,
                                GCancellable       *cancellable,
                                GError            **error)
{
  g_autofree char *sha256 = g_compute_checksum_for_bytes (G_CHECKSUM_SHA256, data);
  g_autofree char *subpath = NULL;

  g_assert (self->valid);

  subpath = g_strdup_printf ("blobs/sha256/%s", sha256);
  if (!glnx_file_replace_contents_at (self->dfd, subpath,
                                      g_bytes_get_data (data, NULL),
                                      g_bytes_get_size (data),
                                      0, cancellable, error))
    return FALSE;

  return g_strdup_printf ("sha256:%s", sha256);
}

static JsonNode *
ostree_oci_registry_load_json_blob (OstreeOciRegistry  *self,
                                    const char         *digest,
                                    GCancellable *cancellable,
                                    GError **error)
{
  g_autoptr(GBytes) bytes = NULL;

  bytes = ostree_oci_registry_load_blob (self, digest, cancellable, error);
  if (bytes == NULL)
    return NULL;

  return parse_json (bytes, cancellable, error);
}

OstreeOciVersioned *
ostree_oci_registry_load_versioned (OstreeOciRegistry  *self,
                                    const char         *digest,
                                    GCancellable       *cancellable,
                                    GError            **error)
{
  g_autoptr(JsonNode) node = NULL;

  g_assert (self->valid);

  node = ostree_oci_registry_load_json_blob (self, digest, cancellable, error);
  if (node == NULL)
    return NULL;

  return ostree_oci_versioned_from_json (node, error);
}
