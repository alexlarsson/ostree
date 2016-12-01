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

#include "ostree-json-private.h"

G_BEGIN_DECLS

#define OSTREE_OCI_MEDIA_TYPE_DESCRIPTOR "application/vnd.oci.descriptor.v1+json"
#define OSTREE_OCI_MEDIA_TYPE_IMAGE_MANIFEST "application/vnd.oci.image.manifest.v1+json"
#define OSTREE_OCI_MEDIA_TYPE_IMAGE_MANIFESTLIST "application/vnd.oci.image.manifest.list.v1+json"
#define OSTREE_OCI_MEDIA_TYPE_IMAGE_LAYER "application/vnd.oci.image.layer.v1.tar+gzip"
#define OSTREE_OCI_MEDIA_TYPE_IMAGE_LAYER_NONDISTRIBUTABLE "application/vnd.oci.image.layer.nondistributable.v1.tar+gzip"
#define OSTREE_OCI_MEDIA_TYPE_IMAGE_CONFIG "application/vnd.oci.image.config.v1+json"

typedef struct {
  char *mediatype;
  char *digest;
  gint64 size;
  char **urls;
} OstreeOciDescriptor;

void ostree_oci_descriptor_destroy (OstreeOciDescriptor *self);
void ostree_oci_descriptor_free (OstreeOciDescriptor *self);

typedef struct
{
  char *architecture;
  char *os;
  char *os_version;
  char **os_features;
  char *variant;
  char **features;
} OstreeOciManifestPlatform;


typedef struct
{
  OstreeOciDescriptor parent;
  OstreeOciManifestPlatform platform;
} OstreeOciManifestDescriptor;

void ostree_oci_manifest_descriptor_destroy (OstreeOciManifestDescriptor *self);
void ostree_oci_manifest_descriptor_free (OstreeOciManifestDescriptor *self);

#define OSTREE_TYPE_OCI_REF ostree_oci_ref_get_type ()
G_DECLARE_FINAL_TYPE (OstreeOciRef, ostree_oci_ref, OSTREE_OCI, REF, OstreeJson)

struct _OstreeOciRef {
  OstreeJson parent;

  OstreeOciDescriptor descriptor;
};

struct _OstreeOciRefClass {
  OstreeJsonClass parent_class;
};

OstreeOciRef * ostree_oci_ref_new (const char *mediatype,
                                   const char *digest,
                                   gint64 size);

#define OSTREE_TYPE_OCI_VERSIONED ostree_oci_versioned_get_type ()
G_DECLARE_FINAL_TYPE (OstreeOciVersioned, ostree_oci_versioned, OSTREE_OCI, VERSIONED, OstreeJson)

struct _OstreeOciVersioned {
  OstreeJson parent;

  int version;
  char *mediatype;
};

struct _OstreeOciVersionedClass {
  OstreeJsonClass parent_class;
};

OstreeOciVersioned * ostree_oci_versioned_from_json (JsonNode *node, GError **error);

#define OSTREE_TYPE_OCI_MANIFEST ostree_oci_manifest_get_type ()
G_DECLARE_FINAL_TYPE (OstreeOciManifest, ostree_oci_manifest, OSTREE, OCI_MANIFEST, OstreeOciVersioned)

struct _OstreeOciManifest
{
  OstreeOciVersioned parent;

  OstreeOciDescriptor config;
  OstreeOciDescriptor **layers;
  GHashTable     *annotations;
};

struct _OstreeOciManifestClass
{
  OstreeOciVersionedClass parent_class;
};

#define OSTREE_TYPE_OCI_MANIFEST_LIST ostree_oci_manifest_list_get_type ()
G_DECLARE_FINAL_TYPE (OstreeOciManifestList, ostree_oci_manifest_list, OSTREE, OCI_MANIFEST_LIST, OstreeOciVersioned)

struct _OstreeOciManifestList
{
  OstreeOciVersioned parent;

  OstreeOciManifestDescriptor **manifests;
  GHashTable     *annotations;
};

struct _OstreeOciManifestListClass
{
  OstreeOciVersionedClass parent_class;
};

#define OSTREE_TYPE_OCI_IMAGE ostree_oci_image_get_type ()
G_DECLARE_FINAL_TYPE (OstreeOciImage, ostree_oci_image, OSTREE, OCI_IMAGE, OstreeJson)

typedef struct
{
  char *type;
  char **diff_ids;
} OstreeOciImageRootfs;

typedef struct
{
  char *user;
  char *working_dir;
  gint64 memory;
  gint64 memory_swap;
  gint64 cpu_shares;
  char **env;
  char **cmd;
  char **entrypoint;
  char **exposed_ports;
  char **volumes;
  GHashTable *labels;
} OstreeOciImageConfig;

typedef struct
{
  char *created;
  char *created_by;
  char *author;
  char *comment;
  gboolean empty_layer;
} OstreeOciImageHistory;

struct _OstreeOciImage
{
  OstreeJson parent;

  char *created;
  char *author;
  char *architecture;
  char *os;
  OstreeOciImageRootfs rootfs;
  OstreeOciImageConfig config;
  OstreeOciImageHistory **history;
};

struct _OstreeOciImageClass
{
  OstreeJsonClass parent_class;
};

G_END_DECLS
