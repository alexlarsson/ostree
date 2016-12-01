/*
 * Copyright (C) 2015 Red Hat, Inc
 *
 * This file is free software; you can redistribute it and/or modify it
 * under the terms of the GNU Lesser General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
 *
 * This file is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 * Authors:
 *       Alexander Larsson <alexl@redhat.com>
 */

#include "config.h"
#include "string.h"

#include "ostree-json-oci-private.h"
#include "libglnx.h"

void
ostree_oci_descriptor_destroy (OstreeOciDescriptor *self)
{
  g_free (self->mediatype);
  g_free (self->digest);
  g_strfreev (self->urls);
}

void
ostree_oci_descriptor_free (OstreeOciDescriptor *self)
{
  ostree_oci_descriptor_destroy (self);
  g_free (self);
}

static OstreeJsonProp ostree_oci_descriptor_props[] = {
  OSTREE_JSON_STRING_PROP (OstreeOciDescriptor, mediatype, "mediaType"),
  OSTREE_JSON_STRING_PROP (OstreeOciDescriptor, digest, "digest"),
  OSTREE_JSON_INT64_PROP (OstreeOciDescriptor, size, "size"),
  OSTREE_JSON_STRV_PROP (OstreeOciDescriptor, urls, "urls"),
  OSTREE_JSON_LAST_PROP
};

static void
ostree_oci_manifest_platform_destroy (OstreeOciManifestPlatform *self)
{
  g_free (self->architecture);
  g_free (self->os);
  g_free (self->os_version);
  g_strfreev (self->os_features);
  g_free (self->variant);
  g_strfreev (self->features);
}

void
ostree_oci_manifest_descriptor_destroy (OstreeOciManifestDescriptor *self)
{
  ostree_oci_manifest_platform_destroy (&self->platform);
  ostree_oci_descriptor_destroy (&self->parent);
}

void
ostree_oci_manifest_descriptor_free (OstreeOciManifestDescriptor *self)
{
  ostree_oci_manifest_descriptor_destroy (self);
  g_free (self);
}

static OstreeJsonProp ostree_oci_manifest_platform_props[] = {
  OSTREE_JSON_STRING_PROP (OstreeOciManifestPlatform, architecture, "architecture"),
  OSTREE_JSON_STRING_PROP (OstreeOciManifestPlatform, os, "os"),
  OSTREE_JSON_STRING_PROP (OstreeOciManifestPlatform, os_version, "os.version"),
  OSTREE_JSON_STRING_PROP (OstreeOciManifestPlatform, variant, "variant"),
  OSTREE_JSON_STRV_PROP (OstreeOciManifestPlatform, os_features, "os.features"),
  OSTREE_JSON_STRV_PROP (OstreeOciManifestPlatform, features, "features"),
  OSTREE_JSON_LAST_PROP
};
static OstreeJsonProp ostree_oci_manifest_descriptor_props[] = {
  OSTREE_JSON_PARENT_PROP (OstreeOciManifestDescriptor, parent, ostree_oci_descriptor_props),
  OSTREE_JSON_STRUCT_PROP (OstreeOciManifestDescriptor, platform, "platform", ostree_oci_manifest_platform_props),
  OSTREE_JSON_LAST_PROP
};

G_DEFINE_TYPE (OstreeOciRef, ostree_oci_ref, OSTREE_TYPE_JSON);

static void
ostree_oci_ref_finalize (GObject *object)
{
  OstreeOciRef *self = OSTREE_OCI_REF (object);

  ostree_oci_descriptor_destroy (&self->descriptor);

  G_OBJECT_CLASS (ostree_oci_ref_parent_class)->finalize (object);
}

static void
ostree_oci_ref_class_init (OstreeOciRefClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  OstreeJsonClass *json_class = OSTREE_JSON_CLASS (klass);
  static OstreeJsonProp props[] = {
    OSTREE_JSON_PARENT_PROP (OstreeOciRef, descriptor, ostree_oci_descriptor_props),
    OSTREE_JSON_LAST_PROP
  };

  object_class->finalize = ostree_oci_ref_finalize;
  json_class->props = props;
}

static void
ostree_oci_ref_init (OstreeOciRef *self)
{
}

OstreeOciRef *
ostree_oci_ref_new (const char *mediatype,
                    const char *digest,
                    gint64 size)
{
  OstreeOciRef *ref;

  ref = g_object_new (OSTREE_TYPE_OCI_REF, NULL);
  ref->descriptor.mediatype = g_strdup (mediatype);
  ref->descriptor.digest = g_strdup (digest);
  ref->descriptor.size = size;

  return ref;
}

G_DEFINE_TYPE (OstreeOciVersioned, ostree_oci_versioned, OSTREE_TYPE_JSON);

static void
ostree_oci_versioned_finalize (GObject *object)
{
  OstreeOciVersioned *self = OSTREE_OCI_VERSIONED (object);

  g_free (self->mediatype);

  G_OBJECT_CLASS (ostree_oci_versioned_parent_class)->finalize (object);
}

static void
ostree_oci_versioned_class_init (OstreeOciVersionedClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  OstreeJsonClass *json_class = OSTREE_JSON_CLASS (klass);
  static OstreeJsonProp props[] = {
    OSTREE_JSON_INT64_PROP (OstreeOciVersioned, version, "schemaVersion"),
    OSTREE_JSON_STRING_PROP (OstreeOciVersioned, mediatype, "mediaType"),
    OSTREE_JSON_LAST_PROP
  };

  object_class->finalize = ostree_oci_versioned_finalize;
  json_class->props = props;
}

static void
ostree_oci_versioned_init (OstreeOciVersioned *self)
{
}

OstreeOciVersioned *
ostree_oci_versioned_from_json (JsonNode *node, GError **error)
{
  JsonObject *object = json_node_get_object (node);
  const gchar *mediatype;

  mediatype = json_object_get_string_member (object, "mediaType");
  if (mediatype == NULL)
    {
      g_set_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT,
                   "Versioned object lacks mediatype");
      return NULL;
    }

  if (strcmp (mediatype, OSTREE_OCI_MEDIA_TYPE_IMAGE_MANIFEST) == 0)
    return (OstreeOciVersioned *) ostree_json_from_node (node, OSTREE_TYPE_OCI_MANIFEST, error);

  if (strcmp (mediatype, OSTREE_OCI_MEDIA_TYPE_IMAGE_MANIFESTLIST) == 0)
    return (OstreeOciVersioned *) ostree_json_from_node (node, OSTREE_TYPE_OCI_MANIFEST_LIST, error);
  g_set_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT,
               "Unsupported media type %s", mediatype);
  return NULL;
}


G_DEFINE_TYPE (OstreeOciManifest, ostree_oci_manifest, OSTREE_TYPE_OCI_VERSIONED);

static void
ostree_oci_manifest_finalize (GObject *object)
{
  OstreeOciManifest *self = (OstreeOciManifest *) object;
  int i;

  for (i = 0; self->layers != NULL && self->layers[i] != NULL; i++)
    ostree_oci_descriptor_free (self->layers[i]);
  g_free (self->layers);
  ostree_oci_descriptor_destroy (&self->config);
  if (self->annotations)
    g_hash_table_destroy (self->annotations);

  G_OBJECT_CLASS (ostree_oci_manifest_parent_class)->finalize (object);
}

static void
ostree_oci_manifest_class_init (OstreeOciManifestClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  OstreeJsonClass *json_class = OSTREE_JSON_CLASS (klass);
  static OstreeJsonProp props[] = {
    OSTREE_JSON_STRUCT_PROP(OstreeOciManifest, config, "config", ostree_oci_descriptor_props),
    OSTREE_JSON_STRUCTV_PROP(OstreeOciManifest, layers, "layers", ostree_oci_descriptor_props),
    OSTREE_JSON_STRMAP_PROP(OstreeOciManifest, annotations, "annotations"),
    OSTREE_JSON_LAST_PROP
  };

  object_class->finalize = ostree_oci_manifest_finalize;
  json_class->props = props;
}

static void
ostree_oci_manifest_init (OstreeOciManifest *self)
{
}


G_DEFINE_TYPE (OstreeOciManifestList, ostree_oci_manifest_list, OSTREE_TYPE_OCI_VERSIONED);

static void
ostree_oci_manifest_list_finalize (GObject *object)
{
  OstreeOciManifestList *self = (OstreeOciManifestList *) object;
  int i;

  for (i = 0; self->manifests != NULL && self->manifests[i] != NULL; i++)
    ostree_oci_manifest_descriptor_free (self->manifests[i]);
  g_free (self->manifests);

  if (self->annotations)
    g_hash_table_destroy (self->annotations);

  G_OBJECT_CLASS (ostree_oci_manifest_list_parent_class)->finalize (object);
}


static void
ostree_oci_manifest_list_class_init (OstreeOciManifestListClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  OstreeJsonClass *json_class = OSTREE_JSON_CLASS (klass);
  static OstreeJsonProp props[] = {
    OSTREE_JSON_STRUCTV_PROP(OstreeOciManifestList, manifests, "manifests", ostree_oci_manifest_descriptor_props),
    OSTREE_JSON_STRMAP_PROP(OstreeOciManifestList, annotations, "annotations"),
    OSTREE_JSON_LAST_PROP
  };

  object_class->finalize = ostree_oci_manifest_list_finalize;
  json_class->props = props;
}

static void
ostree_oci_manifest_list_init (OstreeOciManifestList *self)
{
}

G_DEFINE_TYPE (OstreeOciImage, ostree_oci_image, OSTREE_TYPE_JSON);

static void
ostree_oci_image_rootfs_destroy (OstreeOciImageRootfs *self)
{
  g_free (self->type);
  g_strfreev (self->diff_ids);
}

static void
ostree_oci_image_config_destroy (OstreeOciImageConfig *self)
{
  g_free (self->user);
  g_free (self->working_dir);
  g_strfreev (self->env);
  g_strfreev (self->cmd);
  g_strfreev (self->entrypoint);
  g_strfreev (self->exposed_ports);
  g_strfreev (self->volumes);
  if (self->labels)
    g_hash_table_destroy (self->labels);
}

static void
ostree_oci_image_history_free (OstreeOciImageHistory *self)
{
  g_free (self->created);
  g_free (self->created_by);
  g_free (self->author);
  g_free (self->comment);
  g_free (self);
}

static void
ostree_oci_image_finalize (GObject *object)
{
  OstreeOciImage *self = (OstreeOciImage *) object;
  int i;

  g_free (self->created);
  g_free (self->author);
  g_free (self->architecture);
  g_free (self->os);
  ostree_oci_image_rootfs_destroy (&self->rootfs);
  ostree_oci_image_config_destroy (&self->config);

  for (i = 0; self->history != NULL && self->history[i] != NULL; i++)
    ostree_oci_image_history_free (self->history[i]);
  g_free (self->history);

  G_OBJECT_CLASS (ostree_oci_image_parent_class)->finalize (object);
}

static void
ostree_oci_image_class_init (OstreeOciImageClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  OstreeJsonClass *json_class = OSTREE_JSON_CLASS (klass);
  static OstreeJsonProp config_props[] = {
    OSTREE_JSON_STRING_PROP (OstreeOciImageConfig, user, "User"),
    OSTREE_JSON_INT64_PROP (OstreeOciImageConfig, memory, "Memory"),
    OSTREE_JSON_INT64_PROP (OstreeOciImageConfig, memory_swap, "MemorySwap"),
    OSTREE_JSON_INT64_PROP (OstreeOciImageConfig, cpu_shares, "CpuShares"),
    OSTREE_JSON_BOOLMAP_PROP (OstreeOciImageConfig, exposed_ports, "ExposedPorts"),
    OSTREE_JSON_STRV_PROP (OstreeOciImageConfig, env, "Env"),
    OSTREE_JSON_STRV_PROP (OstreeOciImageConfig, entrypoint, "Entrypoint"),
    OSTREE_JSON_STRV_PROP (OstreeOciImageConfig, cmd, "Cmd"),
    OSTREE_JSON_BOOLMAP_PROP (OstreeOciImageConfig, volumes, "Volumes"),
    OSTREE_JSON_STRING_PROP (OstreeOciImageConfig, working_dir, "WorkingDir"),
    OSTREE_JSON_STRMAP_PROP(OstreeOciImageConfig, labels, "Labels"),
    OSTREE_JSON_LAST_PROP
  };
  static OstreeJsonProp rootfs_props[] = {
    OSTREE_JSON_STRING_PROP (OstreeOciImageRootfs, type, "type"),
    OSTREE_JSON_STRV_PROP (OstreeOciImageRootfs, diff_ids, "diff_ids"),
    OSTREE_JSON_LAST_PROP
  };
  static OstreeJsonProp history_props[] = {
    OSTREE_JSON_STRING_PROP (OstreeOciImageHistory, created, "created"),
    OSTREE_JSON_STRING_PROP (OstreeOciImageHistory, created_by, "created_by"),
    OSTREE_JSON_STRING_PROP (OstreeOciImageHistory, author, "author"),
    OSTREE_JSON_STRING_PROP (OstreeOciImageHistory, comment, "comment"),
    OSTREE_JSON_BOOL_PROP (OstreeOciImageHistory, empty_layer, "empty_layer"),
    OSTREE_JSON_LAST_PROP
  };
  static OstreeJsonProp props[] = {
    OSTREE_JSON_STRING_PROP (OstreeOciImage, created, "created"),
    OSTREE_JSON_STRING_PROP (OstreeOciImage, author, "author"),
    OSTREE_JSON_STRING_PROP (OstreeOciImage, architecture, "architecture"),
    OSTREE_JSON_STRING_PROP (OstreeOciImage, os, "os"),
    OSTREE_JSON_STRUCT_PROP (OstreeOciImage, config, "config", config_props),
    OSTREE_JSON_STRUCT_PROP (OstreeOciImage, rootfs, "rootfs", rootfs_props),
    OSTREE_JSON_STRUCTV_PROP (OstreeOciImage, history, "history", history_props),
    OSTREE_JSON_LAST_PROP
  };

  object_class->finalize = ostree_oci_image_finalize;
  json_class->props = props;

}

static void
ostree_oci_image_init (OstreeOciImage *self)
{
}
