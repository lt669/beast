/* GXK - Gtk+ Extension Kit
 * Copyright (C) 2002-2003 Tim Janik
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General
 * Public License along with this program; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place, Suite 330,
 * Boston, MA 02111-1307, USA.
 */
#include "gxkradget.h"
#include "gxkradgetfactory.h"
#include "gxkauxwidgets.h"
#include "gxkmenubutton.h"
#include "glewidgets.h"
#include "gxksimplelabel.h"
#include "gxkracktable.h"
#include "gxkrackitem.h"
#include <libintl.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>
#include <string.h>
#include <stdlib.h>

#define NODE(n)         ((Node*) n)

struct _GxkGadgetArgs {
  guint    n_variables;
  gboolean intern_quarks;
  GQuark  *quarks;
  gchar  **values;
};
#define ARGS_N_ENTRIES(o)    ((o) ? (o)->n_variables : 0)
#define ARGS_NTH_NAME(o,n)   (g_quark_to_string ((o)->quarks[(n)]))
#define ARGS_NTH_VALUE(o,n)  ((o)->values[(n)])

typedef struct {
  guint null_collapse : 1;
} EnvSpecials;

typedef struct {
  GSList       *args_list; /* GxkGadgetArgs* */
  const gchar  *name;
  EnvSpecials  *specials;
  GData        *hgroups, *vgroups, *hvgroups;
  GxkGadget    *xdef_gadget;
} Env;

typedef struct {
  const gchar *name;
  const gchar *value;
} Prop;
typedef struct Node Node;
struct Node {
  const gchar  *domain;
  const gchar  *name;
  GType         type;
  guint         depth; /* == number of ancestors */
  Node         *xdef_node;
  guint         xdef_depth; /* xdef_node->depth */
  GSList       *parent_arg_list; /* of type GxkGadgetArgs* */
  GxkGadgetArgs *call_args;
  GxkGadgetArgs *scope_args;
  GxkGadgetArgs *prop_args;
  GxkGadgetArgs *pack_args;
  GxkGadgetArgs *dfpk_args;
  const gchar  *size_hgroup;
  const gchar  *size_vgroup;
  const gchar  *size_hvgroup;
  const gchar *default_area;
  GSList       *children;       /* Node* */
  GSList       *call_stack;     /* expanded call_args */
};
typedef struct {
  GData *nodes;
  const gchar *domain;
} Domain;
typedef struct {
  Domain      *domain;
  const gchar *i18n_domain;
  guint        tag_opened;
  GSList      *node_stack; /* Node* */
} PData;                /* parser state */
typedef gchar* (*MacroFunc)     (GSList *args,
                                 Env    *env);


/* --- prototypes --- */
static gchar*          expand_expr              (const gchar         *expr,
                                                 Env                 *env);
static MacroFunc       macro_func_lookup        (const gchar         *name);
static inline gboolean boolean_from_string      (const gchar         *value);
static gboolean        boolean_from_expr        (const gchar         *expr,
                                                 Env                 *env);
static inline guint64  num_from_string          (const gchar         *value);
static guint64         num_from_expr            (const gchar         *expr,
                                                 Env                 *env);
static void            gadget_define_gtk_menu   (void);
static Node*           node_children_find_area  (Node                *node,
                                                 const gchar         *area);
static const gchar*    gadget_args_lookup_quark (const GxkGadgetArgs *args,
                                                 GQuark               quark,
                                                 guint               *nth);


/* --- variables --- */
static Domain *standard_domain = NULL;
static GQuark  quark_name = 0;
static GQuark  quark_gadget_type = 0;
static GQuark  quark_gadget_node = 0;


/* --- functions --- */
static void
set_error (GError     **error,
           const gchar *message_fmt,
           ...)
{
  if (error && !*error)
    {
      va_list args;
      gchar *buffer;
      va_start (args, message_fmt);
      buffer = g_strdup_vprintf (message_fmt, args);
      va_end (args);
      *error = g_error_new_literal (1, 0, buffer);
      (*error)->domain = 0;
      g_free (buffer);
    }
}

#define g_slist_new(d) g_slist_prepend (0, d)

typedef struct {
  Node *source;
  Node *clone;
} NodeClone;
typedef struct {
  guint        n_clones;
  NodeClone   *clones;
} CloneList;

static void
clone_list_add (CloneList *clist,
                Node      *source,
                Node      *clone)
{
  guint i = clist->n_clones++;
  clist->clones = g_renew (NodeClone, clist->clones, clist->n_clones);
  clist->clones[i].source = source;
  clist->clones[i].clone = clone;
}

static Node*
clone_list_find (CloneList *clist,
                 Node      *source)
{
  guint i;
  for (i = 0; i < clist->n_clones; i++)
    if (clist->clones[i].source == source)
      return clist->clones[i].clone;
  g_warning ("failed to find clone for %p", source);
  return NULL;
}

static GxkGadgetArgs*
clone_args (const GxkGadgetArgs *source)
{
  if (source)
    return gxk_gadget_args_merge (gxk_gadget_const_args (), source);
  return NULL;
}

static Node*
clone_node_intern (Node        *source,
                   const gchar *domain,
                   const gchar *name,
                   gboolean     inherit,
                   CloneList   *clist)
{
  Node *node = g_new0 (Node, 1);
  GSList *slist, *last = NULL;
  clone_list_add (clist, source, node);
  node->domain = domain;
  node->name = g_intern_string (name);
  node->type = source->type;
  if (inherit)
    {
      node->depth = source->depth + 1;
      /* no deep copy needed, since source->parent_arg_list are already parsed */
      node->parent_arg_list = source->parent_arg_list;
      /* "add-on" the new modifiable parent_arg_list slot (works only with slists) */
      node->parent_arg_list = g_slist_prepend (node->parent_arg_list, gxk_gadget_const_args ());
    }
  else
    {
      node->depth = source->depth;
      /* no deep copy needed, since source->parent_arg_list are already parsed */
      node->parent_arg_list = source->parent_arg_list;
    }
  if (source->xdef_node)
    {
      g_assert (!inherit);
      node->xdef_node = clone_list_find (clist, source->xdef_node);
      node->xdef_depth = source->xdef_depth;
    }
  node->call_args = clone_args (source->call_args);
  node->scope_args = clone_args (source->scope_args);
  node->prop_args = clone_args (source->prop_args);
  node->pack_args = clone_args (source->pack_args);
  node->dfpk_args = clone_args (source->dfpk_args);
  node->size_hgroup = source->size_hgroup;
  node->size_vgroup = source->size_vgroup;
  node->size_hvgroup = source->size_hvgroup;
  node->default_area = source->default_area;
  for (slist = source->children; slist; slist = slist->next)
    {
      Node *child = slist->data;
      child = clone_node_intern (child, domain, child->name, FALSE, clist);
      if (last)
        {
          last->next = g_slist_new (child);
          last = last->next;
        }
      else
        node->children = last = g_slist_new (child);
    }
  g_assert (source->call_stack == NULL);
  return node;
}

static Node*
clone_node (Node        *source,
            const gchar *domain,
            const gchar *name,
            gboolean     inherit)
{
  CloneList clist = { 0, NULL };
  Node *node = clone_node_intern (source, domain, name, inherit, &clist);
  g_free (clist.clones);
  return node;
}

static inline gboolean
boolean_from_string (const gchar *value)
{
  return !(!value || strlen (value) < 1 || value[0] == '0' ||
           value[0] == 'f' || value[0] == 'F' ||
           value[0] == 'n' || value[0] == 'N');
}

static inline gdouble
float_from_string (const gchar *value)
{
  gdouble v_float = value ? g_strtod (value, NULL) : 0;
  return v_float;
}

static inline guint64
num_from_string (const gchar *value)
{
  gdouble v_float = float_from_string (value);
  v_float = v_float > 0 ? v_float + 0.5 : v_float - 0.5;
  return v_float;
}

static inline gchar
char2eval (const gchar c)
{
  if (c >= '0' && c <= '9')
    return c;
  else if (c >= 'A' && c <= 'Z')
    return c - 'A' + 'a';
  else if (c >= 'a' && c <= 'z')
    return c;
  else
    return '-';
}

static inline gboolean
enum_match (const gchar *str1,
            const gchar *str2)
{
  while (*str1 && *str2)
    {
      guchar s1 = char2eval (*str1++);
      guchar s2 = char2eval (*str2++);
      if (s1 != s2)
        return FALSE;
    }
  return *str1 == 0 && *str2 == 0;
}

static gint
enums_match_value (guint        n_values,
                   GEnumValue  *values,
                   const gchar *name,
                   gint         fallback)
{
  guint i, length = strlen (name);
  if (!name)
    return fallback;
  for (i = 0; i < n_values; i++)
    {
      const gchar *vname = values[i].value_name;
      guint n = strlen (vname);
      if (((n > length && char2eval (vname[n - 1 - length]) == '-')
           || n == length)
          && enum_match (vname + n - length, name))
        return values[i].value;
    }
  for (i = 0; i < n_values; i++)
    {
      const gchar *vname = values[i].value_nick;
      guint n = strlen (vname);
      if (((n > length && char2eval (vname[n - 1 - length]) == '-')
           || n == length)
          && enum_match (vname + n - length, name))
        return values[i].value;
    }
  return fallback;
}

static void
env_clear (Env *env)
{
  g_datalist_clear (&env->hgroups);
  g_datalist_clear (&env->vgroups);
  g_datalist_clear (&env->hvgroups);
}

static GtkSizeGroup*
env_get_size_group (Env         *env,
                    const gchar *name,
                    gchar        type)
{
  GData **groups = type == 'h' ? &env->hgroups : type == 'v' ? &env->vgroups : &env->hvgroups;
  GtkSizeGroup *sg = g_datalist_get_data (groups, name);
  if (!sg)
    {
      sg = gtk_size_group_new (type == 'h' ? GTK_SIZE_GROUP_HORIZONTAL :
                               type == 'v' ? GTK_SIZE_GROUP_VERTICAL :
                               GTK_SIZE_GROUP_BOTH);
      g_datalist_set_data_full (groups, name, sg, g_object_unref);
    }
  return sg;
}

typedef struct _RecursiveOption RecursiveOption;
struct _RecursiveOption {
  RecursiveOption     *next;
  const GxkGadgetArgs *args;
  GQuark               quark;
};
static RecursiveOption *stack_options = NULL;

static const GxkGadgetArgs*
env_find_quark (Env   *env,
                GQuark quark,
                guint *nthp)
{
  GSList *slist;
  for (slist = env->args_list; slist; slist = slist->next)
    {
      GxkGadgetArgs *args = slist->data;
      if (gadget_args_lookup_quark (args, quark, nthp))
        {
          RecursiveOption *ropt;
          for (ropt = stack_options; args && ropt; ropt = ropt->next)
            if (args == ropt->args && quark == ropt->quark)
              args = NULL;
          if (args)
            return args;
        }
    }
  return NULL;
}

static const gchar*
env_lookup (Env         *env,
            const gchar *var)
{
  const GxkGadgetArgs *args;
  GQuark quark = g_quark_try_string (var);
  guint nth;
  if (strcmp (var, "name") == 0)
    return env->name;
  args = quark ? env_find_quark (env, quark, &nth) : NULL;
  return args ? ARGS_NTH_VALUE (args, nth) : NULL;
}

static gchar*
env_expand_args_value (Env                 *env,
                       const GxkGadgetArgs *args,
                       guint                nth)
{
  const gchar *value = ARGS_NTH_VALUE (args, nth);
  gchar *exval;
  RecursiveOption ropt;
  ropt.args = args;
  ropt.quark = args->quarks[nth];
  ropt.next = stack_options;
  stack_options = &ropt;
  exval = expand_expr (value, env);
  stack_options = ropt.next;
  return exval;
}

static inline const gchar*
advance_level (const gchar *c)
{
  guint level = 1;
  do                            /* read til ')' */
    switch (*c++)
      {
      case 0:
        c--;
        level = 0;
        break;
      case '(':
        level++;
        break;
      case ')':
        level--;
        break;
      }
  while (level);
  return c;
}

static inline const gchar*
advance_arg (const gchar *c)
{
  while (*c && *c != ')' && *c != ',')
    if (*c == '$' && c[1] == '(')
      c = advance_level (c + 2);
    else
      c++;
  return c;
}

static const gchar*
parse_formula (const gchar *c,
               GString     *result,
               Env         *env)
{
  const gchar *start = c;
  const gchar *last = c;
  GSList *args = NULL;
  while (*c && *c != ')')
    {
      if (*c == ',')
        {
          args = g_slist_prepend (args, g_strndup (last, c - last));
          last = ++c;
        }
      else
        c = advance_arg (c);
    }
  args = g_slist_prepend (args, g_strndup (last, c - last));
  if (!*c)
    g_printerr ("malformed formula: $(%s", start);
  else
    c++;        /* ')' */
  if (args)
    {
      gchar *str;
      args = g_slist_reverse (args);
      str = macro_func_lookup (args->data) (args, env);
      if (str)
        g_string_append (result, str);
      g_free (str);
      g_slist_foreach (args, (GFunc) g_free, NULL);
      g_slist_free (args);
    }
  return c;
}

static const gchar*
parse_dollar (const gchar *c,
              GString     *result,
              Env         *env)
{
  const gchar *ident_start = G_CSET_A_2_Z G_CSET_a_2_z "_";
  const gchar *ident_chars = G_CSET_A_2_Z G_CSET_a_2_z G_CSET_DIGITS ".-_";
  const gchar *mark = c;
  GQuark quark;
  if (*c == '(')
    return parse_formula (c + 1, result, env);
  if (*c == '$')
    {
      g_string_append_c (result, '$');
      return c + 1;
    }
  if (strchr (ident_start, *c))
    {
      gchar *var;
      c++;
      while (*c && strchr (ident_chars, *c))
        c++;
      var = g_strndup (mark, c - mark);
      quark = g_quark_try_string (var);
      g_free (var);
      if (quark == quark_name)
        g_string_append (result, env->name);
      else
        {
          guint nth;
          const GxkGadgetArgs *args = env_find_quark (env, quark, &nth);
          if (args)
            {
              gchar *exval = env_expand_args_value (env, args, nth);
              if (exval)
                g_string_append (result, exval);
              g_free (exval);
            }
        }
      return c;
    }
  if (*c)       /* eat one non-ident char */
    c++;
  return c;
}

static gchar*
expand_expr (const gchar *expr,
             Env         *env)
{
  GString *result = g_string_new ("");
  const gchar *c = expr;
  const gchar *dollar = strchr (c, '$');
  EnvSpecials *saved_specials = env->specials;
  EnvSpecials specials = { 0, };
  env->specials = &specials;
  while (dollar)
    {
      g_string_append_len (result, c, dollar - c);
      c = parse_dollar (dollar + 1, result, env);
      dollar = strchr (c, '$');
    }
  g_string_append (result, c);
  env->specials = saved_specials;
  if (specials.null_collapse && !result->str[0])
    return g_string_free (result, TRUE);
  else
    return g_string_free (result, FALSE);
}

static guint64
num_from_expr (const gchar    *expr,
               Env            *env)
{
  gchar *result = expand_expr (expr, env);
  guint64 num = num_from_string (result);
  g_free (result);
  return num;
}

static gboolean
boolean_from_expr (const gchar    *expr,
                   Env            *env)
{
  gchar *result = expand_expr (expr, env);
  gboolean boolv = boolean_from_string (result);
  g_free (result);
  return boolv;
}

static Node*
node_children_find_area (Node        *node,
                         const gchar *area)
{
  GSList *children = g_slist_copy (node->children);
  while (children)
    {
      GSList *slist, *newlist = NULL;
      for (slist = children; slist; slist = slist->next)
        if (strcmp (NODE (slist->data)->name, area) == 0)
          {
            node = slist->data;
            g_slist_free (children);
            return node;
          }
      for (slist = children; slist; slist = slist->next)
        newlist = g_slist_concat (g_slist_copy (NODE (slist->data)->children), newlist);
      g_slist_free (children);
      children = newlist;
    }
  return NULL;
}

static Node*
node_find_area (Node        *node,
                const gchar *area)
{
  Node *child;
  const gchar *p;
  if (!area || strcmp ("default", area) == 0)
    area = node->default_area;
  if (!area)
    return node;
  p = strchr (area, '.');
  if (p)
    {
      gchar *str = g_strndup (area, p - area);
      if (strcmp (str, "default") == 0)
        child = !node->default_area ? node : node_children_find_area (node, node->default_area);
      else if (strcmp (str, node->name) == 0)
        child = node;
      else
        child = node_children_find_area (node, str);
      g_free (str);
      if (child)
        return node_find_area (child, p + 1);
      else
        return node_find_area (node, NULL);
    }
  if (strcmp (area, node->name) == 0)
    child = node;
  else
    child = node_children_find_area (node, area);
  if (child)
    return node_find_area (child, NULL);
  else
    return node;
}

static Node*
node_lookup (Domain      *domain,
             const gchar *node_name)
{
  Node *node = g_datalist_get_data (&domain->nodes, node_name);
  if (!node && domain != standard_domain)
    node = g_datalist_get_data (&standard_domain->nodes, node_name);
  return node;
}

static GxkGadgetArgs*
gadget_args_intern_set (GxkGadgetArgs  *args,
                        const gchar    *name,
                        const gchar    *value)
{
  if (!args)
    args = gxk_gadget_const_args ();
  return gxk_gadget_args_set (args, name, value);
}

static Node*
node_define (Domain       *domain,
             const gchar  *node_name,
             GType         direct_type, /* for builtins */
             Node         *source,      /* for calls */
             Node         *xdef_node,   /* for calls */
             const gchar **attribute_names,
             const gchar **attribute_values,
             const gchar  *i18n_domain,
             const gchar **name_p,
             const gchar **area_p,
             const gchar **default_area_p,
             GError       **error)
{
  Node *node = NULL;
  gboolean allow_defs = !source, inherit = FALSE;
  guint i;
  if (direct_type)                              /* builtin definition */
    {
      node = g_new0 (Node, 1);
      node->domain = domain->domain;
      node->name = g_intern_string (node_name);
      node->type = direct_type;
    }
  else if (source)                              /* call */
    {
      node = clone_node (source, domain->domain, node_name, inherit);
      node->xdef_node = xdef_node;
      g_assert (xdef_node && !xdef_node->xdef_node);
      node->xdef_depth = node->xdef_node->depth;
    }
  else for (i = 0; attribute_names[i]; i++)     /* node inheritance */
    if (!node && strcmp (attribute_names[i], "inherit") == 0)
      {
        source = node_lookup (domain, attribute_values[i]);
        inherit = TRUE;
        if (source)
          node = clone_node (source, domain->domain, node_name, inherit);
        break;
      }
  /* apply attributes */
  for (i = 0; attribute_names[i]; i++)
    if (default_area_p && !*default_area_p && strcmp (attribute_names[i], "default-area") == 0)
      {
        *default_area_p = g_intern_string (attribute_values[i]);
      }
    else if (area_p && !*area_p && strcmp (attribute_names[i], "area") == 0)
      {
        *area_p = g_intern_string (attribute_values[i]);
      }
  if (!node)
    set_error (error, "no gadget type specified in definition of: %s", node_name);
  if (*error)
    return NULL;
  /* apply property attributes */
  for (i = 0; attribute_names[i]; i++)
    if (strncmp (attribute_names[i], "pack:", 5) == 0)
      node->pack_args = gadget_args_intern_set (node->pack_args, attribute_names[i] + 5, attribute_values[i]);
    else if (strncmp (attribute_names[i], "default-pack:", 13) == 0)
      node->dfpk_args = gadget_args_intern_set (node->dfpk_args, attribute_names[i] + 13, attribute_values[i]);
    else if (strcmp (attribute_names[i], "name") == 0 || strcmp (attribute_names[i], "_name") == 0)
      {
        if (name_p && !*name_p)
          *name_p = g_intern_string (attribute_values[i]);
      }
    else if (allow_defs && strncmp (attribute_names[i], "prop:", 5) == 0)
      node->prop_args = gadget_args_intern_set (node->prop_args, attribute_names[i] + 5, attribute_values[i]);
    else if (strcmp (attribute_names[i], "size:hgroup") == 0 && g_type_is_a (node->type, GTK_TYPE_WIDGET))
      node->size_hgroup = g_intern_string (attribute_values[i]);
    else if (strcmp (attribute_names[i], "size:vgroup") == 0 && g_type_is_a (node->type, GTK_TYPE_WIDGET))
      node->size_vgroup = g_intern_string (attribute_values[i]);
    else if (strcmp (attribute_names[i], "size:hvgroup") == 0 && g_type_is_a (node->type, GTK_TYPE_WIDGET))
      node->size_hvgroup = g_intern_string (attribute_values[i]);
    else if (strcmp (attribute_names[i], "inherit") == 0 ||
             strcmp (attribute_names[i], "default-area") == 0 ||
             strcmp (attribute_names[i], "area") == 0)
      ; /* handled above */
#if 0   // if at all, this should be part of an extra <local var=value"/> directive
    else if (strncmp (attribute_names[i], "local:", 6) == 0)
      node->scope_args = gadget_args_intern_set (node->scope_args, attribute_names[i] + 6, attribute_values[i]);
#endif
    else if (strchr (attribute_names[i], ':'))
      set_error (error, "invalid attribute \"%s\" in definition of: %s", attribute_names[i], node_name);
    else
      {
        const gchar *name = attribute_names[i];
        const gchar *value = attribute_values[i];
        if (inherit)
          gadget_args_intern_set (node->parent_arg_list->data, name, value);
        else
          node->call_args = gadget_args_intern_set (node->call_args, name, value);
        if (name[0] == '_') /* i18n version */
          {
            if (inherit)
              gadget_args_intern_set (node->parent_arg_list->data, name + 1, dgettext (i18n_domain, value));
            else
              node->call_args = gadget_args_intern_set (node->call_args, name + 1, dgettext (i18n_domain, value));
          }
      }
  if (!g_type_is_a (node->type, G_TYPE_OBJECT))
    set_error (error, "no gadget type specified in definition of: %s", node_name);
  return node;
}

static void             /* callback for open tags <foo bar="baz"> */
gadget_start_element  (GMarkupParseContext *context,
                       const gchar         *element_name,
                       const gchar        **attribute_names,
                       const gchar        **attribute_values,
                       gpointer             user_data,
                       GError             **error)
{
  PData *pdata = user_data;
  Node *child;
  if (!pdata->tag_opened && strcmp (element_name, "gxk-gadget-definitions") == 0)
    {
      /* toplevel tag */
      pdata->tag_opened = TRUE;
    }
  else if (pdata->node_stack == NULL && strncmp (element_name, "xdef:", 5) == 0)
    {
      const gchar *name = element_name + 5;
      if (g_datalist_get_data (&pdata->domain->nodes, name))
        set_error (error, "redefinition of gadget: %s", name);
      else
        {
          const gchar *default_area = NULL;
          Node *node = node_define (pdata->domain, name, 0, NULL, NULL, attribute_names, attribute_values,
                                    pdata->i18n_domain, NULL, NULL, &default_area, error);
          if (node)
            {
              if (default_area)
                node->default_area = default_area;
              g_datalist_set_data (&pdata->domain->nodes, name, node);
              pdata->node_stack = g_slist_prepend (pdata->node_stack, node);
            }
        }
    }
  else if (pdata->node_stack && (child = node_lookup (pdata->domain, element_name), child))
    {
      Node *parent = pdata->node_stack->data;
      const gchar *area = NULL, *uname = NULL;
      Node *node = node_define (pdata->domain, element_name, 0, child, g_slist_last (pdata->node_stack)->data,
                                attribute_names, attribute_values, pdata->i18n_domain,
                                &uname, &area, NULL, error);
      if (node)
        {
          if (uname)
            node->name = uname;
          parent = node_find_area (parent, area);
          parent->children = g_slist_append (parent->children, node);
          pdata->node_stack = g_slist_prepend (pdata->node_stack, node);
        }
      else
        set_error (error, "failed to define gadget: %s", element_name);
    }
  else
    set_error (error, "unknown element: %s", element_name);
}

static void             /* callback for close tags </foo> */
gadget_end_element (GMarkupParseContext *context,
                    const gchar         *element_name,
                    gpointer             user_data,
                    GError             **error)
{
  PData *pdata = user_data;
  if (strcmp (element_name, "gxk-gadget-definitions") == 0)
    {
      /* toplevel tag closed */
      pdata->tag_opened = FALSE;
    }
  else if (pdata->node_stack && pdata->node_stack->next == NULL && strncmp (element_name, "xdef:", 5) == 0)
    {
      g_slist_pop_head (&pdata->node_stack);
    }
  else if (pdata->node_stack && pdata->node_stack->next)
    {
      g_slist_pop_head (&pdata->node_stack);
    }
}

static void             /* callback for character data */
gadget_text (GMarkupParseContext *context,
             const gchar         *text,    /* text is not 0-terminated */
             gsize                text_len,
             gpointer             user_data,
             GError             **error)
{
  // PData *pdata = user_data;
}

static void             /* callback for comments and processing instructions */
gadget_passthrough (GMarkupParseContext *context,
                    const gchar         *passthrough_text, /* text is not 0-terminated. */
                    gsize                text_len,
                    gpointer             user_data,
                    GError             **error)
{
  // PData *pdata = user_data;
}

static void             /* callback for errors, including ones set by other methods in the vtable */
gadget_error (GMarkupParseContext *context,
              GError              *error,  /* the GError should not be freed */
              gpointer             user_data)
{
  // PData *pdata = user_data;
}

static void
gadget_parser (Domain      *domain,
               const gchar *i18n_domain,
               gint         fd,
               const gchar *text,
               gint         length,
               GError     **error)
{
  static GMarkupParser parser = {
    gadget_start_element,
    gadget_end_element,
    gadget_text,
    gadget_passthrough,
    gadget_error,
  };
  PData pbuf = { 0, }, *pdata = &pbuf;
  GMarkupParseContext *context = g_markup_parse_context_new (&parser, 0, pdata, NULL);
  guint8 bspace[1024];
  const gchar *buffer = text ? text : (const gchar*) bspace;
  pdata->domain = domain;
  pdata->i18n_domain = i18n_domain;
  if (!text)
    length = read (fd, bspace, 1024);
  while (length > 0)
    {
      if (!g_markup_parse_context_parse (context, buffer, length, error))
        break;
      if (!text)
        length = read (fd, bspace, 1024);
      else
        length = 0;
    }
  if (length < 0)
    set_error (error, "failed to read gadget file: %s", g_strerror (errno));
  if (!*error)
    g_markup_parse_context_end_parse (context, error);
  g_markup_parse_context_free (context);
}

static GData *domains = NULL;

void
gxk_gadget_parse (const gchar    *domain_name,
                  const gchar    *file_name,
                  const gchar    *i18n_domain,
                  GError        **error)
{
  Domain *domain;
  GError *myerror = NULL;
  gint fd = open (file_name, O_RDONLY, 0);
  domain = domain_name ? g_datalist_get_data (&domains, domain_name) : standard_domain;
  if (!domain)
    {
      domain = g_new0 (Domain, 1);
      domain->domain = g_intern_string (domain_name);
      g_datalist_set_data (&domains, domain_name, domain);
    }
  gadget_parser (domain, i18n_domain, fd, NULL, 0, error ? error : &myerror);
  close (fd);
  if (myerror)
    {
      g_warning ("GxkGadget: while parsing \"%s\": %s", file_name, myerror->message);
      g_error_free (myerror);
    }
}

void
gxk_gadget_parse_text (const gchar    *domain_name,
                       const gchar    *text,
                       gint            text_len,
                       const gchar    *i18n_domain,
                       GError        **error)
{
  Domain *domain;
  GError *myerror = NULL;
  g_return_if_fail (text != NULL);
  domain = domain_name ? g_datalist_get_data (&domains, domain_name) : standard_domain;
  if (!domain)
    {
      domain = g_new0 (Domain, 1);
      domain->domain = g_intern_string (domain_name);
      g_datalist_set_data (&domains, domain_name, domain);
    }
  gadget_parser (domain, i18n_domain, -1, text, text_len < 0 ? strlen (text) : text_len, error ? error : &myerror);
  if (myerror)
    {
      g_warning ("GxkGadget: while parsing: %s", myerror->message);
      g_error_free (myerror);
    }
}

static void
property_value_from_string (GtkType      widget_type,
                            GParamSpec  *pspec,
                            GValue      *value,
                            const gchar *pname,
                            const gchar *pvalue,
                            Env         *env,
                            GError     **error)
{
  GType vtype = G_PARAM_SPEC_VALUE_TYPE (pspec);
  gint edefault = 0;
  gchar *exvalue;
  if (G_IS_PARAM_SPEC_ENUM (pspec))
    edefault = G_PARAM_SPEC_ENUM (pspec)->default_value;
  else if (g_type_is_a (widget_type, GTK_TYPE_TEXT_TAG) &&
           strcmp (pname, "weight") == 0)
    {
      /* special case GtkTextTag::weight which is an enum really */
      vtype = PANGO_TYPE_WEIGHT;
      edefault = G_PARAM_SPEC_INT (pspec)->default_value;
    }
  exvalue = expand_expr (pvalue, env);
  switch (G_TYPE_FUNDAMENTAL (vtype))
    {
      GEnumClass *eclass;
      GFlagsClass *fclass;
      gdouble v_float;
    case G_TYPE_BOOLEAN:
      g_value_init (value, G_TYPE_BOOLEAN);
      g_value_set_boolean (value, boolean_from_string (exvalue));
      break;
    case G_TYPE_STRING:
      g_value_init (value, G_TYPE_STRING);
      g_value_set_string (value, exvalue);
      break;
    case G_TYPE_INT:
    case G_TYPE_UINT:
    case G_TYPE_LONG:
    case G_TYPE_ULONG:
      g_value_init (value, G_TYPE_FUNDAMENTAL (vtype));
      v_float = float_from_string (exvalue);
      v_float = v_float > 0 ? v_float + 0.5 : v_float - 0.5;
      switch (G_TYPE_FUNDAMENTAL (vtype))
        {
        case G_TYPE_INT:        g_value_set_int (value, v_float); break;
        case G_TYPE_UINT:       g_value_set_uint (value, v_float); break;
        case G_TYPE_LONG:       g_value_set_long (value, v_float); break;
        case G_TYPE_ULONG:      g_value_set_ulong (value, v_float); break;
        }
      break;
    case G_TYPE_FLOAT:
    case G_TYPE_DOUBLE:
      g_value_init (value, G_TYPE_DOUBLE);
      g_value_set_double (value, float_from_string (exvalue));
      break;
    case G_TYPE_ENUM:
      eclass = g_type_class_peek (vtype);
      if (eclass)
        {
          g_value_init (value, vtype);
          g_value_set_enum (value, enums_match_value (eclass->n_values, eclass->values, exvalue, edefault));
        }
      break;
    case G_TYPE_FLAGS:
      fclass = g_type_class_peek (vtype);
      if (fclass && exvalue)
        {
          gchar **fnames = g_strsplit (exvalue, "|", -1);
          guint i, v = 0;
          g_value_init (value, vtype);
          for (i = 0; fnames[i]; i++)
            v |= enums_match_value (fclass->n_values, (GEnumValue*) fclass->values, fnames[i], 0);
          g_value_set_flags (value, v);
          g_strfreev (fnames);
        }
      break;
    default:
      set_error (error, "unsupported property: %s::%s", g_type_name (widget_type), pname);
      break;
    }
  if (0 && G_VALUE_TYPE (value) && strchr (pvalue, '$'))
    g_print ("property[%s]: expr=%s result=%s GValue=%s\n", pspec->name, pvalue, exvalue, g_strdup_value_contents (value));
  g_free (exvalue);
}

static GxkGadgetArgs*
merge_args_list (GxkGadgetArgs *args,
                 GSList        *call_args)
{
  if (call_args)
    {
      if (call_args->next)
        args = merge_args_list (args, call_args->next);
      args = gxk_gadget_args_merge (args, call_args->data);
    }
  return args;
}

static GxkGadgetArgs*
node_expand_call_args (Node   *node,
                       GSList *call_args,
                       Env    *env)
{
  /* precedence for value lookups:
   * x for call_args intra lookups are _not_ possible
   * - xdef scope_args
   * - xdef call_args
   * - xdef ancestry call_args (dependant on parent level)
   * $name is special cased in the value lookup function
   */
  guint i, n_pops = 0;
  /* flatten call args */
  GxkGadgetArgs *args = gxk_gadget_args (NULL);
  args = merge_args_list (args, call_args);
  args = gxk_gadget_args_merge (args, node->call_args);
  /* prepare for $name lookups */
  env->name = node->name;
  /* push args lists according to precedence */
  if (node->xdef_node)
    {
      guint n = node->xdef_node->depth;
      GSList *polist, *slist = NULL;
      for (polist = node->xdef_node->parent_arg_list; n > node->xdef_depth; n--, polist = polist->next)
        slist = g_slist_prepend (slist, polist->data);
      while (slist)     /* two times prepending keeps original order */
        {
          GxkGadgetArgs *pargs = g_slist_pop_head (&slist);
          n_pops++, env->args_list = g_slist_prepend (env->args_list, pargs);
        }
    }
  if (node->xdef_node && node->xdef_node->call_stack)
    n_pops++, env->args_list = g_slist_prepend (env->args_list, node->xdef_node->call_stack->data);
  if (node->xdef_node && node->xdef_node->scope_args)
    n_pops++, env->args_list = g_slist_prepend (env->args_list, node->xdef_node->scope_args);
  /* expand args */
  for (i = 0; i < ARGS_N_ENTRIES (args); i++)
    {
      gchar *value = ARGS_NTH_VALUE (args, i);
      if (value && strchr (value, '$'))
        {
          gchar *exval = env_expand_args_value (env, args, i);
          ARGS_NTH_VALUE (args, i) = exval;
          g_free (value);
        }
    }
  /* cleanup */
  while (n_pops--)
    g_slist_pop_head (&env->args_list);
  return args;
}

struct GxkGadgetData {
  Node         *node;
  GxkGadgetArgs *call_stack_top;
  GxkGadget    *xdef_gadget;
  Env          *env;
};

static GxkGadget*
gadget_create_from_node (Node         *node,
                         GxkGadget    *gadget,
                         Env          *env,
                         GError      **error)
{
  GxkGadgetType tinfo;
  guint i, n_pops = 0;
  /* prepare for $name lookups */
  env->name = node->name;
  /* retrive type info */
  if (!gxk_gadget_type_lookup (node->type, &tinfo))
    g_error ("invalid gadget type: %s", g_type_name (node->type));
  /* create gadget */
  if (!gadget)
    {
      GxkGadgetData gdgdata;
      gdgdata.node = node;
      gdgdata.call_stack_top = node->call_stack->data;
      gdgdata.xdef_gadget = env->xdef_gadget;
      gdgdata.env = env;
      gadget = tinfo.create (node->type, node->name, &gdgdata);
    }
  g_object_set_qdata (gadget, quark_gadget_node, node);
  /* keep global xdef_gadget for gdg_data */
  if (!env->xdef_gadget)
    env->xdef_gadget = gadget;
  /* widget specific patchups (size-groups) */
  if (node->size_hgroup)
    gtk_size_group_add_widget (env_get_size_group (env, node->size_hgroup, 'h'), gadget);
  if (node->size_vgroup)
    gtk_size_group_add_widget (env_get_size_group (env, node->size_vgroup, 'v'), gadget);
  if (node->size_hvgroup)
    gtk_size_group_add_widget (env_get_size_group (env, node->size_hvgroup, 'b'), gadget);
  /* precedence for property value lookups:
   * - all node ancestry args
   * - expanded call_args
   */
  if (node->call_stack)
    n_pops++, env->args_list = g_slist_prepend (env->args_list, node->call_stack->data);
  if (node->parent_arg_list)
    n_pops += g_slist_length (node->parent_arg_list), env->args_list = g_slist_concat (g_slist_copy (node->parent_arg_list),
                                                                                       env->args_list);
  /* set properties */
  for (i = 0; i < ARGS_N_ENTRIES (node->prop_args); i++)
    {
      const gchar *pname = ARGS_NTH_NAME (node->prop_args, i);
      const gchar *pvalue = ARGS_NTH_VALUE (node->prop_args, i);
      GParamSpec *pspec = tinfo.find_prop (gadget, pname);
      if (pspec)
        {
          GValue value = { 0 };
          property_value_from_string (node->type, pspec, &value, pname, pvalue, env, error);
          if (G_VALUE_TYPE (&value))
            {
              tinfo.set_prop (gadget, pname, &value);
              g_value_unset (&value);
            }
        }
      else
        set_error (error, "gadget \"%s\" has no property: %s", node->name, pname);
    }
  /* cleanup */
  while (n_pops--)
    g_slist_pop_head (&env->args_list);
  return gadget;
}

static void
gadget_add_to_parent (GxkGadget    *parent,
                      GxkGadget    *gadget,
                      Env          *env,
                      GError      **error)
{
  Node *pnode = g_object_get_qdata (parent, quark_gadget_node);
  Node *cnode = g_object_get_qdata (gadget, quark_gadget_node);
  GxkGadgetType tinfo;
  guint i, needs_packing, n_pops = 0;
  /* prepare for $name lookups */
  env->name = cnode->name;
  /* retrive type info */
  gxk_gadget_type_lookup (cnode->type, &tinfo);
  /* precedence for property value lookups:
   * - all node ancestry args
   * - expanded call_args
   */
  if (cnode->call_stack)
    n_pops++, env->args_list = g_slist_prepend (env->args_list, cnode->call_stack->data);
  if (cnode->parent_arg_list)
    n_pops += g_slist_length (cnode->parent_arg_list), env->args_list = g_slist_concat (g_slist_copy (cnode->parent_arg_list),
                                                                                        env->args_list);
  /* perform set_parent() */
  {
    GxkGadgetData gdgdata;
    gdgdata.node = cnode;
    gdgdata.call_stack_top = cnode->call_stack->data;
    gdgdata.xdef_gadget = env->xdef_gadget;
    gdgdata.env = env;
    needs_packing = tinfo.adopt (gadget, parent, &gdgdata);
  }
  /* construct set of pack args and apply */
  if (needs_packing)
    {
      GxkGadgetArgs *args = gxk_gadget_args_merge (gxk_gadget_const_args (),
                                                   pnode ? pnode->dfpk_args : NULL);
      args = gxk_gadget_args_merge (args, cnode->pack_args);
      /* set pack args */
      for (i = 0; i < ARGS_N_ENTRIES (args); i++)
        {
          const gchar *pname = ARGS_NTH_NAME (args, i);
          const gchar *pvalue = ARGS_NTH_VALUE (args, i);
          GParamSpec *pspec = tinfo.find_pack (gadget, pname);
          if (pspec)
            {
              GValue value = { 0 };
              property_value_from_string (0, pspec, &value, pname, pvalue, env, error);
              if (G_VALUE_TYPE (&value))
                {
                  tinfo.set_pack (gadget, pname, &value);
                  g_value_unset (&value);
                }
            }
          else
            g_printerr ("GXK: no such pack property: %s,%s,%s\n", G_OBJECT_TYPE_NAME (parent), G_OBJECT_TYPE_NAME (gadget), pname);
        }
      gxk_gadget_free_args (args);
    }
  /* cleanup */
  while (n_pops--)
    g_slist_pop_head (&env->args_list);
}

static void
gadget_create_children (GxkGadget    *parent,
                        Env          *env,
                        GError      **error)
{
  Node *pnode = g_object_get_qdata (parent, quark_gadget_node);
  GSList *slist;
  /* create children */
  for (slist = pnode->children; slist; slist = slist->next)
    {
      Node *cnode = slist->data;
      GxkGadget *gadget;
      /* node_expand_call_args() sets env->name */
      GxkGadgetArgs *call_args = node_expand_call_args (cnode, NULL, env);
      cnode->call_stack = g_slist_prepend (cnode->call_stack, call_args);
      /* create child */
      gadget = gadget_create_from_node (cnode, NULL, env, error);
      if (cnode->children)
        gadget_create_children (gadget, env, error);
      gadget_add_to_parent (parent, gadget, env, error);
      g_slist_pop_head (&cnode->call_stack);
      gxk_gadget_free_args (call_args);
    }
}

static GxkGadget*
gadget_creator (GxkGadget          *gadget,
                const gchar        *domain_name,
                const gchar        *name,
                GxkGadget          *parent,
                GSList             *user_args,
                GSList             *env_args)
{
  Domain *domain = g_datalist_get_data (&domains, domain_name);
  if (domain)
    {
      Node *node = g_datalist_get_data (&domain->nodes, name);
      if (node)
        {
          GxkGadgetArgs *call_args;
          Env env = { NULL, };
          GError *error = NULL;
          guint n_pops = 0;
          if (env_args)
            {
              n_pops += g_slist_length (node->parent_arg_list);
              env.args_list = g_slist_concat (g_slist_copy (env_args), env.args_list);
            }
          call_args = node_expand_call_args (node, user_args, &env);
          n_pops++, node->call_stack = g_slist_prepend (node->call_stack, call_args);
          if (gadget && !g_type_is_a (G_OBJECT_TYPE (gadget), node->type))
            g_warning ("GxkGadget: gadget domain \"%s\": gadget `%s' differs from defined type: %s",
                       domain_name, G_OBJECT_TYPE_NAME (gadget), node->name);
          else
            {
              gadget = gadget_create_from_node (node, gadget, &env, &error);
              gadget_create_children (gadget, &env, &error);
            }
          if (parent && gadget)
            gadget_add_to_parent (parent, gadget, &env, &error);
          /* cleanup */
          while (n_pops--)
            g_slist_pop_head (&env.args_list);
          gxk_gadget_free_args (call_args);
          env_clear (&env);
          if (error)
            g_warning ("GxkGadget: while constructing gadget \"%s\": %s", node->name, error->message);
          g_clear_error (&error);
        }
      else
        g_warning ("GxkGadget: gadget domain \"%s\": no such node: %s", domain_name, name);
    }
  else
    g_warning ("GxkGadget: no such gadget domain: %s", domain_name);
  return gadget;
}

GxkGadgetArgs*
gxk_gadget_data_copy_call_args (GxkGadgetData *gdgdata)
{
  GxkGadgetArgs *args;
  GSList *olist = NULL;
  olist = g_slist_copy (gdgdata->node->parent_arg_list);
  olist = g_slist_prepend (olist, gdgdata->call_stack_top);
  args = node_expand_call_args (gdgdata->node, olist, gdgdata->env);
  g_slist_free (olist);
  return args;
}

GxkGadget*
gxk_gadget_data_get_scope_gadget (GxkGadgetData *gdgdata)
{
  return gdgdata->xdef_gadget;
}

GxkGadget*
gxk_gadget_creator (GxkGadget          *gadget,
                    const gchar        *domain_name,
                    const gchar        *name,
                    GxkGadget          *parent,
                    GSList             *call_args,
                    GSList             *env_args)
{
  g_return_val_if_fail (domain_name != NULL, NULL);
  g_return_val_if_fail (name != NULL, NULL);
  if (gadget)
    {
      Node *gadget_node = g_object_get_qdata (gadget, quark_gadget_node);
      g_return_val_if_fail (gadget_node == NULL, NULL);
    }
  return gadget_creator (gadget, domain_name, name, parent, call_args, env_args);
}

GxkGadget*
gxk_gadget_create (const gchar        *domain_name,
                   const gchar        *name,
                   const gchar        *var1,
                   ...)
{
  GxkGadgetArgs *gargs;
  GxkGadget *gadget;
  GSList olist = { 0, };
  va_list vargs;
  va_start (vargs, var1);
  gargs = gxk_gadget_args_valist (var1, vargs);
  olist.data = gargs;
  gadget = gxk_gadget_creator (NULL, domain_name, name, NULL, &olist, NULL);
  gxk_gadget_free_args (gargs);
  va_end (vargs);
  return gadget;
}

GxkGadget*
gxk_gadget_complete (GxkGadget          *gadget,
                     const gchar        *domain_name,
                     const gchar        *name,
                     const gchar        *var1,
                     ...)
{
  GxkGadgetArgs *gargs;
  GSList olist = { 0, };
  va_list vargs;
  va_start (vargs, var1);
  gargs = gxk_gadget_args_valist (var1, vargs);
  olist.data = gargs;
  gadget = gxk_gadget_creator (gadget, domain_name, name, NULL, &olist, NULL);
  gxk_gadget_free_args (gargs);
  va_end (vargs);
  return gadget;
}

GxkGadgetArgs*
gxk_gadget_const_args (void)
{
  GxkGadgetArgs *args = g_new0 (GxkGadgetArgs, 1);
  args->intern_quarks = TRUE;
  return args;
}

GxkGadgetArgs*
gxk_gadget_args_valist (const gchar        *name1,
                        va_list             var_args)
{
  GxkGadgetArgs *args = g_new0 (GxkGadgetArgs, 1);
  const gchar *name = name1;
  while (name)
    {
      const gchar *value = va_arg (var_args, const gchar*);
      args = gxk_gadget_args_set (args, name, value);
      name = va_arg (var_args, const gchar*);
    }
  return args;
}

GxkGadgetArgs*
gxk_gadget_args (const gchar *name1,
                 ...)
{
  GxkGadgetArgs *args;
  va_list vargs;
  va_start (vargs, name1);
  args = gxk_gadget_args_valist (name1, vargs);
  va_end (vargs);
  return args;
}

GxkGadgetArgs*
gxk_gadget_args_set (GxkGadgetArgs  *args,
                     const gchar    *name,
                     const gchar    *value)
{
  GQuark quark = g_quark_from_string (name);
  guint i;
  g_return_val_if_fail (name != NULL, args);
  if (!args)
    args = gxk_gadget_args (NULL);
  for (i = 0; i < ARGS_N_ENTRIES (args); i++)
    if (quark == args->quarks[i])
      break;
  if (i >= ARGS_N_ENTRIES (args))
    {
      i = args->n_variables++;
      args->quarks = g_renew (GQuark, args->quarks, ARGS_N_ENTRIES (args));
      args->values = g_renew (gchar*, args->values, ARGS_N_ENTRIES (args));
      args->quarks[i] = quark;
    }
  else if (!args->intern_quarks)
    g_free (args->values[i]);
  if (args->intern_quarks)
    args->values[i] = (gchar*) g_intern_string (value);
  else
    args->values[i] = g_strdup (value);
  return args;
}

static const gchar*
gadget_args_lookup_quark (const GxkGadgetArgs *args,
                          GQuark               quark,
                          guint               *nthp)
{
  guint i;
  for (i = 0; i < ARGS_N_ENTRIES (args); i++)
    if (quark == args->quarks[i])
      {
        if (nthp)
          *nthp = i;
        return ARGS_NTH_VALUE (args, i);
      }
  return NULL;
}

const gchar*
gxk_gadget_args_get (const GxkGadgetArgs *args,
                     const gchar         *name)
{
  GQuark quark = g_quark_try_string (name);
  if (args && quark)
    return gadget_args_lookup_quark (args, quark, NULL);
  return NULL;
}

GxkGadgetArgs*
gxk_gadget_args_merge (GxkGadgetArgs       *args,
                       const GxkGadgetArgs *source)
{
  if (source)
    {
      guint i;
      if (!args)
        args = gxk_gadget_args (NULL);
      for (i = 0; i < ARGS_N_ENTRIES (source); i++)
        gxk_gadget_args_set (args, ARGS_NTH_NAME (source, i), ARGS_NTH_VALUE (source, i));
    }
  return args;
}

void
gxk_gadget_free_args (GxkGadgetArgs *args)
{
  if (args)
    {
      guint i;
      if (!args->intern_quarks)
        for (i = 0; i < ARGS_N_ENTRIES (args); i++)
          g_free (args->values[i]);
      g_free (args->values);
      g_free (args->quarks);
      g_free (args);
    }
}

const gchar*
gxk_gadget_get_domain (GxkGadget *gadget)
{
  Node *gadget_node = g_object_get_qdata (gadget, quark_gadget_node);
  g_return_val_if_fail (gadget_node != NULL, NULL);
  return gadget_node->domain;
}

void
gxk_gadget_sensitize (GxkGadget      *gadget,
                      const gchar    *name,
                      gboolean        sensitive)
{
  GtkWidget *widget = gxk_gadget_find (gadget, name);
  if (GTK_IS_WIDGET (widget))
    {
      /* special guard for menu items */
      if (sensitive && GTK_IS_MENU_ITEM (widget))
        {
          GtkMenuItem *mitem = GTK_MENU_ITEM (widget);
          if (mitem && mitem->submenu)
            sensitive = gxk_menu_check_sensitive (GTK_MENU (mitem->submenu));
        }
      gtk_widget_set_sensitive (widget, sensitive);
    }
}

gpointer
gxk_gadget_find (GxkGadget      *gadget,
                 const gchar    *name)
{
  const gchar *next, *c = name;
  
  g_return_val_if_fail (gadget != NULL, NULL);
  g_return_val_if_fail (name != NULL, NULL);
  
  if (!GTK_IS_WIDGET (gadget))
    return NULL;
  
  next = strchr (c, '.');
  while (gadget && next)
    {
      gchar *name = g_strndup (c, next - c);
      c = next + 1;
      gadget = gxk_widget_find_level_ordered (gadget, name);
      g_free (name);
    }
  if (gadget)
    gadget = gxk_widget_find_level_ordered (gadget, c);
  return gadget;
}

gpointer
gxk_gadget_find_area (GxkGadget      *gadget,
                      const gchar    *area)
{
  Node *node;
  gadget = area ? gxk_gadget_find (gadget, area) : gadget;
  if (!GTK_IS_WIDGET (gadget))
    return NULL;
  node = g_object_get_qdata (gadget, quark_gadget_node);
  while (node && node->default_area)
    {
      gadget = gxk_widget_find_level_ordered (gadget, node->default_area);
      node = gadget ? g_object_get_qdata (gadget, quark_gadget_node) : NULL;
    }
  return gadget;
}

void
gxk_gadget_add (GxkGadget      *gadget,
                const gchar    *area,
                gpointer        widget)
{
  g_return_if_fail (GTK_IS_WIDGET (widget));
  gadget = gxk_gadget_find_area (gadget, area);
  if (GTK_IS_CONTAINER (gadget))
    gtk_container_add (gadget, widget);
  else
    g_error ("GxkGadget: failed to find area \"%s\"", area);
}


/* --- gadget types --- */
static void
gadget_define_type (GType           type,
                    const gchar    *name,
                    const gchar   **attribute_names,
                    const gchar   **attribute_values,
                    const gchar    *i18n_domain)
{
  GError *error = NULL;
  Node *node;
  node = node_define (standard_domain, name, type, NULL, NULL,
                      attribute_names, attribute_values,
                      i18n_domain, NULL, NULL, NULL, &error);
  g_datalist_set_data (&standard_domain->nodes, name, node);
  if (error)
    g_error ("while registering standard gadgets: %s", error->message);
}

void
_gxk_init_gadget_types (void)
{
  GType types[1024], *t = types;
  g_assert (quark_gadget_type == 0);
  quark_name = g_quark_from_static_string ("name");
  quark_gadget_type = g_quark_from_static_string ("GxkGadget-type");
  quark_gadget_node = g_quark_from_static_string ("GxkGadget-node");
  standard_domain = g_new0 (Domain, 1);
  standard_domain->domain = g_intern_string ("standard");
  g_datalist_set_data (&domains, standard_domain->domain, standard_domain);
  *t++ = GTK_TYPE_WINDOW;       *t++ = GTK_TYPE_ARROW;          *t++ = GTK_TYPE_SCROLLED_WINDOW;
  *t++ = GTK_TYPE_TABLE;        *t++ = GTK_TYPE_FRAME;          *t++ = GTK_TYPE_ALIGNMENT;
  *t++ = GTK_TYPE_NOTEBOOK;     *t++ = GTK_TYPE_BUTTON;         *t++ = GTK_TYPE_MENU_BAR;
  *t++ = GTK_TYPE_TREE_VIEW;    *t++ = GTK_TYPE_LABEL;          *t++ = GTK_TYPE_PROGRESS_BAR;
  *t++ = GTK_TYPE_HPANED;       *t++ = GTK_TYPE_VPANED;         *t++ = GTK_TYPE_SPIN_BUTTON;
  *t++ = GTK_TYPE_EVENT_BOX;    *t++ = GTK_TYPE_IMAGE;          *t++ = GTK_TYPE_OPTION_MENU;
  *t++ = GTK_TYPE_HBOX;         *t++ = GTK_TYPE_VBOX;           *t++ = GXK_TYPE_MENU_BUTTON;
  *t++ = GTK_TYPE_CHECK_BUTTON; *t++ = GTK_TYPE_ENTRY;          *t++ = GXK_TYPE_MENU_ITEM;
  *t++ = GTK_TYPE_HSCROLLBAR;   *t++ = GTK_TYPE_HSCALE;         *t++ = GTK_TYPE_TEAROFF_MENU_ITEM;
  *t++ = GTK_TYPE_VSCROLLBAR;   *t++ = GTK_TYPE_VSCALE;         *t++ = GXK_TYPE_IMAGE;
  *t++ = GTK_TYPE_VSEPARATOR;   *t++ = GXK_TYPE_SIMPLE_LABEL;   *t++ = GTK_TYPE_HSEPARATOR;
  *t++ = GTK_TYPE_HWRAP_BOX;    *t++ = GTK_TYPE_VWRAP_BOX;      *t++ = GXK_TYPE_FREE_RADIO_BUTTON;
  *t++ = GXK_TYPE_RACK_TABLE;   *t++ = GXK_TYPE_RACK_ITEM;	*t++ = GXK_TYPE_BACK_SHADE;
  while (t-- > types)
    gxk_gadget_define_widget_type (*t);
  gadget_define_gtk_menu ();
  gxk_gadget_define_type (GXK_TYPE_GADGET_FACTORY, _gxk_gadget_factory_def);
  gxk_gadget_define_type (GXK_TYPE_FACTORY_BRANCH, _gxk_factory_branch_def);
  gxk_gadget_define_type (GXK_TYPE_WIDGET_PATCHER, _gxk_widget_patcher_def);
}

gboolean
gxk_gadget_type_lookup (GType           type,
                        GxkGadgetType  *ggtype)
{
  GxkGadgetType *tdata = g_type_get_qdata (type, quark_gadget_type);
  if (tdata)
    {
      *ggtype = *tdata;
      return TRUE;
    }
  return FALSE;
}

void
gxk_gadget_define_type (GType                type,
                        const GxkGadgetType *ggtype)
{
  const gchar *attribute_names[1] = { NULL };
  const gchar *attribute_values[1] = { NULL };
  
  g_return_if_fail (!G_TYPE_IS_ABSTRACT (type));
  g_return_if_fail (G_TYPE_IS_OBJECT (type));
  g_return_if_fail (g_type_get_qdata (type, quark_gadget_type) == NULL);
  
  g_type_set_qdata (type, quark_gadget_type, (gpointer) ggtype);
  gadget_define_type (type, g_type_name (type), attribute_names, attribute_values, NULL);
}


/* --- widget types --- */
static GxkGadget*
widget_create (GType               type,
               const gchar        *name,
               GxkGadgetData      *gdgdata)
{
  return g_object_new (type, "name", name, NULL);
}

static GParamSpec*
widget_find_prop (GxkGadget    *gadget,
                  const gchar  *prop_name)
{
  return g_object_class_find_property (G_OBJECT_GET_CLASS (gadget), prop_name);
}

static gboolean
widget_adopt (GxkGadget          *gadget,
              GxkGadget          *parent,
              GxkGadgetData      *gdgdata)
{
  gtk_container_add (GTK_CONTAINER (parent), GTK_WIDGET (gadget));
  return TRUE;
}

static GParamSpec*
widget_find_pack (GxkGadget    *gadget,
                  const gchar  *pack_name)
{
  GtkWidget *parent = GTK_WIDGET (gadget)->parent;
  return gtk_container_class_find_child_property (G_OBJECT_GET_CLASS (parent), pack_name);
}

static void
widget_set_pack (GxkGadget    *gadget,
                 const gchar  *pack_name,
                 const GValue *value)
{
  GtkWidget *parent = GTK_WIDGET (gadget)->parent;
  gtk_container_child_set_property (GTK_CONTAINER (parent), gadget, pack_name, value);
}

void
gxk_gadget_define_widget_type (GType type)
{
  static const GxkGadgetType widget_info = {
    widget_create,
    widget_find_prop,
    (void(*)(GxkGadget*,const gchar*,const GValue*)) g_object_set_property,
    widget_adopt,
    widget_find_pack,
    widget_set_pack,
  };
  static const struct { const gchar *name, *value; } widget_def[] = {
    {   "prop:visible",         "$(first-occupied,$visible,1)" },
    {   "prop:sensitive",       "$(first-occupied,$sensitive,1)" },
    {   "prop:width-request",   "$(first-occupied,$width,0)" },
    {   "prop:height-request",  "$(first-occupied,$height,0)" },
    {   "prop:events",          "$events" },
    {   "prop:can-focus",       "$(first-occupied,$can-focus,$focus,1)" },
    {   "prop:has-focus",       "$(first-occupied,$has-focus,$focus,0)" },
    {   "prop:can-default",     "$(first-occupied,$can-default,$default,0)" },
    {   "prop:has-default",     "$(first-occupied,$has-default,$default,0)" },
    {   "prop:receives-default","$(first-occupied,$receives-default,0)" },
    {   "prop:extension-events","$extension-events" },
    // gtk+-2.4: {   "prop:no-show-all",     "$(first-occupied,$no-show-all,$hidden,0)" },
  };
  static const struct { const gchar *name, *value; } container_def[] = {
    {   "prop:border-width",    "$(first-occupied,$border-width,0)" },
  };
  const gchar *attribute_names[G_N_ELEMENTS (widget_def) + G_N_ELEMENTS (container_def) + 1];
  const gchar *attribute_values[G_N_ELEMENTS (widget_def) + G_N_ELEMENTS (container_def) + 1];
  guint i, j = 0;
  
  g_return_if_fail (!G_TYPE_IS_ABSTRACT (type));
  g_return_if_fail (g_type_is_a (type, GTK_TYPE_WIDGET));
  g_return_if_fail (g_type_get_qdata (type, quark_gadget_type) == NULL);
  
  g_type_set_qdata (type, quark_gadget_type, (gpointer) &widget_info);
  for (i = 0; i < G_N_ELEMENTS (widget_def); i++)
    {
      attribute_names[j] = widget_def[i].name;
      attribute_values[j] = widget_def[i].value;
      j++;
    }
  if (g_type_is_a (type, GTK_TYPE_CONTAINER))
    for (i = 0; i < G_N_ELEMENTS (container_def); i++)
      {
        attribute_names[j] = container_def[i].name;
        attribute_values[j] = container_def[i].value;
        j++;
      }
  attribute_names[j] = NULL;
  attribute_values[j] = NULL;
  gadget_define_type (type, g_type_name (type), attribute_names, attribute_values, NULL);
}

static gboolean
menu_adopt (GxkGadget          *gadget,
            GxkGadget          *parent,
            GxkGadgetData      *gdgdata)
{
  if (GTK_IS_MENU_ITEM (parent))
    gxk_menu_attach_as_submenu (gadget, parent);
  else if (GTK_IS_OPTION_MENU (parent))
    gtk_option_menu_set_menu (parent, gadget);
  else if (GXK_IS_MENU_BUTTON (parent))
    g_object_set (parent, "menu", gadget, NULL);
  else
    gxk_menu_attach_as_popup (gadget, parent);
  return TRUE;
}

static void* return_NULL (void) { return NULL; }

static void
gadget_define_gtk_menu (void)
{
  static const GxkGadgetType widget_info = {
    widget_create,
    widget_find_prop,
    (void(*)(GxkGadget*,const gchar*,const GValue*)) g_object_set_property,
    menu_adopt,
    (void*) return_NULL,/* find_pack */
    NULL,               /* set_pack */
  };
  const gchar *attribute_names[2] = { NULL, NULL };
  const gchar *attribute_values[2] = { NULL, NULL };
  GType type = GTK_TYPE_MENU;
  g_type_set_qdata (type, quark_gadget_type, (gpointer) &widget_info);
  attribute_names[0] = "prop:visible";
  attribute_values[0] = "$(ifdef,visible,$visible,1)";
  gadget_define_type (type, g_type_name (type), attribute_names, attribute_values, NULL);
}


/* --- macro functions --- */
static inline const gchar*
argiter_pop (GSList **slist_p)
{
  gpointer d = NULL;
  if (*slist_p)
    {
      d = (*slist_p)->data;
      *slist_p = (*slist_p)->next;
    }
  return d;
}

static inline gchar*
argiter_exp (GSList **slist_p,
             Env     *env)
{
  const gchar *s = argiter_pop (slist_p);
  return s ? expand_expr (s, env) : NULL;
}

static gchar*
mf_if (GSList *args,
       Env    *env)
{
  GSList      *argiter = args->next; /* skip func name */
  gchar       *cond = argiter_exp (&argiter, env);
  const gchar *then = argiter_pop (&argiter);
  const gchar *elze = argiter_pop (&argiter);
  gboolean b = boolean_from_string (cond);
  g_free (cond);
  if (b)
    return then ? expand_expr (then, env) : g_strdup ("1");
  else
    return elze ? expand_expr (elze, env) : g_strdup ("0");
}

static gchar*
mf_not (GSList *args,
        Env    *env)
{
  GSList      *argiter = args->next; /* skip func name */
  gchar       *cond = argiter_exp (&argiter, env);
  gboolean b = boolean_from_string (cond);
  g_free (cond);
  return g_strdup (b ? "0" : "1");
}

static gchar*
mf_blogic (GSList *args,
           Env    *env)
{
  GSList      *argiter = args;
  const gchar *name = argiter_pop (&argiter);
  gboolean result = strcmp (name, "and") == 0 || strcmp (name, "nand") == 0;
  while (argiter)
    {
      gchar *vbool = argiter_exp (&argiter, env);
      gboolean b = boolean_from_string (vbool), result;
      g_free (vbool);
      if (strcmp (name, "xor") == 0)
        result ^= b;
      else if (strcmp (name, "or") == 0 || strcmp (name, "nor") == 0)
        result |= b;
      else if (strcmp (name, "and") == 0 || strcmp (name, "nand") == 0)
        result &= b;
    }
  if (strcmp (name, "nor") == 0 || strcmp (name, "nand") == 0)
    result = !result;
  return g_strdup (result ? "1" : "0");
}

#define EQ_FLAG 0x80

static gchar*
mf_floatcmp (GSList *args,
             Env    *env)
{
  GSList      *argiter = args;
  const gchar *name = argiter_pop (&argiter);
  gchar *arg = argiter_exp (&argiter, env);
  double last = arg ? float_from_string (arg) : 0;
  gboolean result = TRUE;
  g_free (arg);
  arg = argiter_exp (&argiter, env);
  while (arg)
    {
      gboolean match = FALSE;
      double v = float_from_string (arg);
      g_free (arg);
      if (strcmp (name, "<") == 0 || strcmp (name, "lt") == 0)
        match = last < v;
      else if (strcmp (name, "<=") == 0 || strcmp (name, "le") == 0)
        match = last <= v;
      else if (strcmp (name, ">") == 0 || strcmp (name, "gt") == 0)
        match = last > v;
      else if (strcmp (name, ">=") == 0 || strcmp (name, "ge") == 0)
        match = last >= v;
      else if (strcmp (name, "!=") == 0 || strcmp (name, "ne") == 0)
        match = last != v;
      else if (strcmp (name, "==") == 0 || strcmp (name, "eq") == 0 || strcmp (name, "=") == 0)
        match = last == v;
      if (!match)
        {
          result = match;
          break;
        }
      arg = argiter_exp (&argiter, env);
    }
  return g_strdup (result ? "1" : "0");
}

static gchar*
mf_first_occupied (GSList *args,
                   Env    *env)
{
  GSList      *argiter = args->next; /* skip func name */
  gchar       *name = argiter_exp (&argiter, env);
  while (name && !name[0])
    {
      g_free (name);
      name = argiter_exp (&argiter, env);
    }
  return name ? name : g_strdup ("");
}

static gchar*
mf_ifdef (GSList *args,
          Env    *env)
{
  GSList      *argiter = args->next; /* skip func name */
  gchar       *name = argiter_exp (&argiter, env);
  const gchar *then = argiter_pop (&argiter);
  const gchar *elze = argiter_pop (&argiter);
  gboolean b = env_lookup (env, name) != NULL;
  g_free (name);
  if (b)
    return then ? expand_expr (then, env) : g_strdup ("1");
  else
    return elze ? expand_expr (elze, env) : g_strdup ("0");
}

static gchar*
mf_nth (GSList *args,
        Env    *env)
{
  GSList *argiter = args->next; /* skip func name */
  gchar  *num = argiter_exp (&argiter, env);
  guint i = num_from_string (num);
  const gchar *d = g_slist_nth_data (args->next, i);
  g_free (num);
  return d ? expand_expr (d, env) : NULL;
}

static gchar*
mf_null_collapse (GSList *args,
                  Env    *env)
{
  GSList *argiter = args->next; /* skip func name */
  gchar  *value = argiter_exp (&argiter, env);
  if ((!value || value[0] == 0) && env->specials)
    env->specials->null_collapse = TRUE;
  return value;
}

static gchar*
mf_empty (GSList *args,
          Env    *env)
{
  return g_strdup ("");
}

static gchar*
mf_println (GSList *args,
            Env    *env)
{
  GSList      *argiter = args->next; /* skip func name */
  while (argiter)
    {
      gchar *arg = argiter_exp (&argiter, env);
      g_print ("%s", arg ? arg : "(null)");
      g_free (arg);
    }
  g_print ("\n");
  return NULL;
}

static MacroFunc
macro_func_lookup (const gchar *name)
{
  static const struct { const gchar *name; MacroFunc mfunc; } macros[] = {
    { "if",             mf_if, },
    { "not",            mf_not, },
    { "xor",            mf_blogic, },
    { "or",             mf_blogic, },
    { "nor",            mf_blogic, },
    { "and",            mf_blogic, },
    { "nand",           mf_blogic, },
    { "<",              mf_floatcmp, },
    { "<=",             mf_floatcmp, },
    { ">",              mf_floatcmp, },
    { ">=",             mf_floatcmp, },
    { "!=",             mf_floatcmp, },
    { "=",              mf_floatcmp, },
    { "==",             mf_floatcmp, },
    { "lt",             mf_floatcmp, },
    { "le",             mf_floatcmp, },
    { "gt",             mf_floatcmp, },
    { "ge",             mf_floatcmp, },
    { "ne",             mf_floatcmp, },
    { "eq",             mf_floatcmp, },
    { "nth",            mf_nth, },
    { "ifdef",          mf_ifdef, },
    { "first-occupied", mf_first_occupied, },
    { "null-collapse",  mf_null_collapse, },
    { "empty",          mf_empty, },
    { "println",        mf_println, },
  };
  guint i;
  for (i = 0; i < G_N_ELEMENTS (macros); i++)
    if (strcmp (name, macros[i].name) == 0)
      return macros[i].mfunc;
  return mf_empty;
}
