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

#include "ostree-json-private.h"
#include "libglnx.h"

G_DEFINE_TYPE (OstreeJson, ostree_json, G_TYPE_OBJECT);

static void
ostree_json_finalize (GObject *object)
{
  G_OBJECT_CLASS (ostree_json_parent_class)->finalize (object);
}

static void
ostree_json_class_init (OstreeJsonClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->finalize = ostree_json_finalize;
}

static void
ostree_json_init (OstreeJson *self)
{
}

static gboolean
demarshal (JsonNode *parent_node,
           const char *name,
           gpointer dest,
           OstreeJsonPropType type,
           gpointer type_data,
           gpointer type_data2,
           GError **error)
{
  JsonObject *parent_object;
  JsonNode *node;

  if (type != OSTREE_JSON_PROP_TYPE_PARENT)
    {
      parent_object = json_node_get_object (parent_node);
      node = json_object_get_member (parent_object, name);
    }
  else
    node = parent_node;

  if (node == NULL || JSON_NODE_TYPE (node) == JSON_NODE_NULL)
    return TRUE;

  switch (type)
    {
    case OSTREE_JSON_PROP_TYPE_STRING:
      if (!JSON_NODE_HOLDS_VALUE (node) ||
          json_node_get_value_type (node) != G_TYPE_STRING)
        {
          g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED,
                       "Expecting string for property %s", name);
          return FALSE;
        }
      *(char **)dest = g_strdup (json_node_get_string (node));
      break;

    case OSTREE_JSON_PROP_TYPE_INT64:
      if (!JSON_NODE_HOLDS_VALUE (node) ||
          json_node_get_value_type (node) != G_TYPE_INT64)
        {
          g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED,
                       "Expecting int64 for property %s", name);
          return FALSE;
        }
      *(gint64 *)dest = json_node_get_int (node);
      break;

    case OSTREE_JSON_PROP_TYPE_BOOL:
      if (!JSON_NODE_HOLDS_VALUE (node) ||
          json_node_get_value_type (node) != G_TYPE_BOOLEAN)
        {
          g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED,
                       "Expecting bool for property %s", name);
          return FALSE;
        }
      *(gboolean *)dest = json_node_get_boolean (node);
      break;

    case OSTREE_JSON_PROP_TYPE_STRV:
      if (!JSON_NODE_HOLDS_ARRAY (node))
        {
          g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED,
                       "Expecting array for property %s", name);
          return FALSE;
        }
      {
        JsonArray *array = json_node_get_array (node);
        guint i, array_len = json_array_get_length (array);
        g_autoptr(GPtrArray) str_array = g_ptr_array_sized_new (array_len + 1);

        for (i = 0; i < array_len; i++)
          {
            JsonNode *val = json_array_get_element (array, i);

            if (JSON_NODE_TYPE (val) != JSON_NODE_VALUE)
              continue;

            if (json_node_get_string (val) != NULL)
              g_ptr_array_add (str_array, (gpointer) g_strdup (json_node_get_string (val)));
          }

        g_ptr_array_add (str_array, NULL);
        *(char ***)dest = (char **)g_ptr_array_free (g_steal_pointer (&str_array), FALSE);
      }

      break;

    case OSTREE_JSON_PROP_TYPE_PARENT:
    case OSTREE_JSON_PROP_TYPE_STRUCT:
      if (!JSON_NODE_HOLDS_OBJECT (node))
        {
          g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED,
                       "Expecting object for property %s", name);
          return FALSE;
        }
      {
        OstreeJsonProp *struct_props = type_data;
        int i;

        for (i = 0; struct_props[i].name != NULL; i++)
          {
            if (!demarshal (node, struct_props[i].name,
                            G_STRUCT_MEMBER_P (dest, struct_props[i].offset),
                            struct_props[i].type, struct_props[i].type_data, struct_props[i].type_data2,
                            error))
              return FALSE;
          }
      }
      break;

    case OSTREE_JSON_PROP_TYPE_STRUCTV:
      if (!JSON_NODE_HOLDS_ARRAY (node))
        {
          g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED,
                       "Expecting array for property %s", name);
          return FALSE;
        }
      {
        JsonArray *array = json_node_get_array (node);
        guint array_len = json_array_get_length (array);
        OstreeJsonProp *struct_props = type_data;
        g_autoptr(GPtrArray) obj_array = g_ptr_array_sized_new (array_len + 1);
        int i, j;
        gboolean res = TRUE;

        for (j = 0; res && j < array_len; j++)
          {
            JsonNode *val = json_array_get_element (array, j);
            gpointer new_element;

            if (!JSON_NODE_HOLDS_OBJECT (val))
              {
                g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED,
                             "Expecting object elemen for property %s", name);
                res = FALSE;
                break;
              }

            new_element = g_malloc0 ((gsize)type_data2);
            g_ptr_array_add (obj_array, new_element);

            for (i = 0; struct_props[i].name != NULL; i++)
              {
                if (!demarshal (val, struct_props[i].name,
                                G_STRUCT_MEMBER_P (new_element, struct_props[i].offset),
                                struct_props[i].type, struct_props[i].type_data, struct_props[i].type_data2,
                                error))
                  {
                    res = FALSE;
                    break;
                  }
              }

          }

        /* NULL terminate */
        g_ptr_array_add (obj_array, NULL);

        /* We always set the array, even if it is partial, because we don't know how
           to free what we demarshalled so far */
        *(gpointer *)dest = (gpointer *)g_ptr_array_free (g_steal_pointer (&obj_array), FALSE);
        return res;
      }
      break;

    case OSTREE_JSON_PROP_TYPE_STRMAP:
      if (!JSON_NODE_HOLDS_OBJECT (node))
        {
          g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED,
                       "Expecting object for property %s", name);
          return FALSE;
        }
      {
        g_autoptr(GHashTable) h = NULL;
        JsonObject *object = json_node_get_object (node);
        g_autoptr(GList) members = NULL;
        GList *l;

        h = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, g_free);

        members = json_object_get_members (object);
        for (l = members; l != NULL; l = l->next)
          {
            const char *member_name = l->data;
            JsonNode *val;
            const char *val_str;

            val = json_object_get_member (object, member_name);
            val_str = json_node_get_string (val);
            if (val_str == NULL)
              {
                g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED,
                             "Wrong type for string member %s", member_name);
                return FALSE;
              }

            g_hash_table_insert (h, g_strdup (member_name), g_strdup (val_str));
          }

        *(GHashTable **)dest = g_steal_pointer (&h);
      }
      break;

    case OSTREE_JSON_PROP_TYPE_BOOLMAP:
      if (!JSON_NODE_HOLDS_OBJECT (node))
        {
          g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED,
                       "Expecting object for property %s", name);
          return FALSE;
        }
      {
        JsonObject *object = json_node_get_object (node);
        g_autoptr(GPtrArray) res = g_ptr_array_new_with_free_func (g_free);
        g_autoptr(GList) members = NULL;
        GList *l;

        members = json_object_get_members (object);
        for (l = members; l != NULL; l = l->next)
          {
            const char *member_name = l->data;

            g_ptr_array_add (res, g_strdup (member_name));
          }

        g_ptr_array_add (res, NULL);

        *(char ***)dest =  (char **)g_ptr_array_free (g_steal_pointer (&res), FALSE);
      }
      break;

    default:
      g_assert_not_reached ();
    }

  return TRUE;
}

OstreeJson *
ostree_json_from_node (JsonNode *node, GType type, GError **error)
{
  g_autoptr(OstreeJson) json = NULL;
  OstreeJsonProp *props = NULL;
  gpointer class;
  int i;

  /* We should handle these before we get here */
  g_assert (node != NULL);
  g_assert (JSON_NODE_TYPE (node) != JSON_NODE_NULL);

  if (JSON_NODE_TYPE (node) != JSON_NODE_OBJECT)
    {
      g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED,
                   "Expecting a JSON object, but the node is of type `%s'",
                   json_node_type_name (node));
      return NULL;
    }

  json = g_object_new (type, NULL);

  class = OSTREE_JSON_GET_CLASS (json);
  while (OSTREE_JSON_CLASS (class)->props != NULL)
    {
      props = OSTREE_JSON_CLASS (class)->props;
      for (i = 0; props[i].name != NULL; i++)
        {
          if (!demarshal (node, props[i].name,
                          G_STRUCT_MEMBER_P (json, props[i].offset),
                          props[i].type, props[i].type_data, props[i].type_data2,
                          error))
            return NULL;
        }
      class = g_type_class_peek_parent (class);
    }

  return g_steal_pointer (&json);
}

static JsonNode *
marshal (JsonObject *parent,
         const char *name,
         gpointer src,
         OstreeJsonPropType type,
         gpointer type_data)
{
  JsonNode *retval = NULL;

  switch (type)
    {
    case OSTREE_JSON_PROP_TYPE_STRING:
      {
        const char *str = *(const char **)src;
        if (str != NULL)
          retval = json_node_init_string (json_node_alloc (), str);
        break;
      }

    case OSTREE_JSON_PROP_TYPE_INT64:
      {
        retval = json_node_init_int (json_node_alloc (), *(gint64 *)src);
        break;
      }

    case OSTREE_JSON_PROP_TYPE_BOOL:
      {
        gboolean val = *(gboolean *)src;
        if (val)
          retval = json_node_init_boolean (json_node_alloc (), val);
        break;
      }

    case OSTREE_JSON_PROP_TYPE_STRV:
      {
        char **strv = *(char ***)src;
        int i;
        JsonArray *array;

        if (strv != NULL && strv[0] != NULL)
          {
            array = json_array_sized_new (g_strv_length (strv));
            for (i = 0; strv[i] != NULL; i++)
              {
                JsonNode *str = json_node_new (JSON_NODE_VALUE);

                json_node_set_string (str, strv[i]);
                json_array_add_element (array, str);
              }

            retval = json_node_init_array (json_node_alloc (), array);
            json_array_unref (array);
          }
        break;
      }

    case OSTREE_JSON_PROP_TYPE_PARENT:
    case OSTREE_JSON_PROP_TYPE_STRUCT:
      {
        OstreeJsonProp *struct_props = type_data;
        JsonObject *obj;
        int i;

        if (type == OSTREE_JSON_PROP_TYPE_PARENT)
          obj = parent;
        else
          obj = json_object_new ();

        for (i = 0; struct_props[i].name != NULL; i++)
          {
            JsonNode *val = marshal (obj, struct_props[i].name,
                                     G_STRUCT_MEMBER_P (src, struct_props[i].offset),
                                     struct_props[i].type, struct_props[i].type_data);

            if (val == NULL)
              continue;

            json_object_set_member (obj, struct_props[i].name, val);
          }

        if (type != OSTREE_JSON_PROP_TYPE_PARENT)
          {
            retval = json_node_new (JSON_NODE_OBJECT);
            json_node_take_object (retval, obj);
          }
        break;
      }

    case OSTREE_JSON_PROP_TYPE_STRUCTV:
      {
        OstreeJsonProp *struct_props = type_data;
        gpointer *structv = *(gpointer **)src;
        int i, j;
        JsonArray *array;

        if (structv != NULL && structv[0] != NULL)
          {
            array = json_array_new ();
            for (j = 0; structv[j] != NULL; j++)
              {
                JsonObject *obj = json_object_new ();
                JsonNode *node;

                for (i = 0; struct_props[i].name != NULL; i++)
                  {
                    JsonNode *val = marshal (obj, struct_props[i].name,
                                             G_STRUCT_MEMBER_P (structv[j], struct_props[i].offset),
                                             struct_props[i].type, struct_props[i].type_data);

                    if (val == NULL)
                      continue;

                    json_object_set_member (obj, struct_props[i].name, val);
                  }

                node = json_node_new (JSON_NODE_OBJECT);
                json_node_take_object (node, obj);
                json_array_add_element (array, node);
              }

            retval = json_node_init_array (json_node_alloc (), array);
            json_array_unref (array);
          }

        break;
      }

    case OSTREE_JSON_PROP_TYPE_STRMAP:
      {
        GHashTable *map = *(GHashTable **)src;

        if (map != NULL && g_hash_table_size (map) > 0)
          {
            GHashTableIter iter;
            gpointer _key, _value;
            JsonObject *object;

            object = json_object_new ();

            g_hash_table_iter_init (&iter, map);
            while (g_hash_table_iter_next (&iter, &_key, &_value))
              {
                const char *key = _key;
                const char *value = _value;
                JsonNode *str = json_node_new (JSON_NODE_VALUE);

                json_node_set_string (str, value);
                json_object_set_member (object, key, str);
              }

            retval = json_node_init_object (json_node_alloc (), object);
            json_object_unref (object);
          }
        break;
      }

    case OSTREE_JSON_PROP_TYPE_BOOLMAP:
      {
        char **map = *(char ***)src;

        if (map != NULL && map[0] != NULL)
          {
            JsonObject *object;
            int i;

            object = json_object_new ();

            for (i = 0; map[i] != NULL; i++)
              {
                const char *element = map[i];
                JsonObject *empty_o = json_object_new ();
                JsonNode *empty = json_node_init_object (json_node_alloc (), empty_o);
                json_object_unref (empty_o);

                json_object_set_member (object, element, empty);
              }

            retval = json_node_init_object (json_node_alloc (), object);
            json_object_unref (object);
          }
        break;
      }

    default:
      g_assert_not_reached ();
    }

  return retval;
}

static void
marshal_props_for_class (OstreeJson *self,
                         OstreeJsonClass *class,
                         JsonObject *obj)
{
  OstreeJsonProp *props = NULL;
  int i;
  gpointer parent_class;

  parent_class = g_type_class_peek_parent (class);

  if (OSTREE_JSON_CLASS (parent_class)->props != NULL)
    marshal_props_for_class (self,
                             OSTREE_JSON_CLASS(parent_class),
                             obj);

  props = OSTREE_JSON_CLASS (class)->props;
  for (i = 0; props[i].name != NULL; i++)
    {
      JsonNode *val = marshal (obj, props[i].name,
                               G_STRUCT_MEMBER_P (self, props[i].offset),
                               props[i].type, props[i].type_data);

      if (val == NULL)
        continue;

      json_object_set_member (obj, props[i].name, val);
    }
}

JsonNode *
ostree_json_to_node (OstreeJson *self)
{
  JsonNode *retval;
  JsonObject *obj;
  gpointer class;

  if (self == NULL)
    json_node_new (JSON_NODE_NULL);

  obj = json_object_new ();

  class = OSTREE_JSON_GET_CLASS (self);
  marshal_props_for_class (self, OSTREE_JSON_CLASS (class), obj);

  retval = json_node_new (JSON_NODE_OBJECT);
  json_node_take_object (retval, obj);

  return retval;
}

GBytes *
ostree_json_to_bytes (OstreeJson  *self)
{
  g_autoptr(JsonNode) node = NULL;
  char *str;

  node = ostree_json_to_node (OSTREE_JSON (self));

  str = json_to_string (node, TRUE);
  return g_bytes_new_take (str, strlen (str));
}
