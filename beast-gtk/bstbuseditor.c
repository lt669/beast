/* BEAST - Bedevilled Audio System
 * Copyright (C) 2004 Tim Janik
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307, USA.
 */
#include "bstbuseditor.h"
#include "bstparam.h"
#include "bstitemseqdialog.h" // FIXME
#include "bstsnifferscope.h" // FIXME


/* --- prototypes --- */
static void     bus_editor_action_exec           (gpointer                data,
                                                  gulong                  action);
static gboolean bus_editor_action_check          (gpointer                data,
                                                  gulong                  action);


/* --- bus actions --- */
enum {
  ACTION_ADD_BUS,
  ACTION_DELETE_BUS,
  ACTION_EDIT_BUS
};
static const GxkStockAction bus_editor_actions[] = {
  { N_("Add"),          NULL,   NULL,   ACTION_ADD_BUS,        BST_STOCK_PART },
  { N_("Delete"),       NULL,   NULL,   ACTION_DELETE_BUS,     BST_STOCK_TRASHCAN },
  { N_("Editor"),       NULL,   NULL,   ACTION_EDIT_BUS,       BST_STOCK_PART_EDITOR },
};


/* --- functions --- */
G_DEFINE_TYPE (BstBusEditor, bst_bus_editor, GTK_TYPE_ALIGNMENT);

static void
popup_item_seq_changed (gpointer             data,
                        BseItemSeq          *iseq,
                        BstItemSeqDialog    *isdialog)
{
  BstBusEditor *self = BST_BUS_EDITOR (data);
  SfiSeq *seq = bse_item_seq_to_seq (iseq);
  GValue *value = sfi_value_seq (seq);
  bse_proxy_set_property (self->item, "inputs", value);
  sfi_value_free (value);
}

static void
popup_item_seq (BstBusEditor *self)
{
  BsePropertyCandidates *pc = bse_item_get_property_candidates (self->item, "inputs");
  GParamSpec *pspec = bse_proxy_get_pspec (self->item, "inputs");
  const GValue *value = bse_proxy_get_property (self->item, "inputs");
  SfiSeq *seq = g_value_get_boxed (value);
  BseItemSeq *iseq = bse_item_seq_from_seq (seq);
  GtkWidget *dialog = bst_item_seq_dialog_popup (self,
                                                 self->item,
                                                 pc->nick, pc->tooltip, pc->items,
                                                 g_param_spec_get_nick (pspec), g_param_spec_get_blurb (pspec), iseq,
                                                 popup_item_seq_changed,
                                                 self);
  (void) dialog;
  bse_item_seq_free (iseq);
}

static void
bst_bus_editor_init (BstBusEditor *self)
{
  /* complete GUI */
  gxk_radget_complete (GTK_WIDGET (self), "beast", "bus-editor", NULL);
  /* create tool actions */
  gxk_widget_publish_actions (self, "bus-editor-actions",
                              G_N_ELEMENTS (bus_editor_actions), bus_editor_actions,
                              NULL, bus_editor_action_check, bus_editor_action_exec);
  GtkWidget *button = gxk_radget_find (self, "bus-outputs");
  g_signal_connect_object (button, "clicked", G_CALLBACK (popup_item_seq), self, G_CONNECT_SWAPPED);
}

static void
bst_bus_editor_destroy (GtkObject *object)
{
  BstBusEditor *self = BST_BUS_EDITOR (object);
  bst_bus_editor_set_bus (self, 0);
  GTK_OBJECT_CLASS (bst_bus_editor_parent_class)->destroy (object);
}

static void
bst_bus_editor_finalize (GObject *object)
{
  BstBusEditor *self = BST_BUS_EDITOR (object);
  bst_bus_editor_set_bus (self, 0);
  G_OBJECT_CLASS (bst_bus_editor_parent_class)->finalize (object);
}

GtkWidget*
bst_bus_editor_new (SfiProxy bus)
{
  g_return_val_if_fail (BSE_IS_BUS (bus), NULL);
  GtkWidget *widget = g_object_new (BST_TYPE_BUS_EDITOR, NULL);
  BstBusEditor *self = BST_BUS_EDITOR (widget);
  bst_bus_editor_set_bus (self, bus);
  return widget;
}

static void
bus_editor_release_item (SfiProxy      item,
                         BstBusEditor *self)
{
  g_assert (self->item == item);
  bst_bus_editor_set_bus (self, 0);
}

static void
bus_probes_notify (SfiProxy     bus,
                   SfiSeq      *sseq,
                   gpointer     data)
{
  BstBusEditor *self = BST_BUS_EDITOR (data);
  BseProbeSeq *pseq = bse_probe_seq_from_seq (sseq);
  BseProbe *lprobe = NULL, *rprobe = NULL;
  guint i;
  for (i = 0; i < pseq->n_probes && (!lprobe || !rprobe); i++)
    if (pseq->probes[i]->channel_id == 0)
      lprobe = pseq->probes[i];
    else if (pseq->probes[i]->channel_id == 1)
      rprobe = pseq->probes[i];
  if (self->lbeam && lprobe && lprobe->probe_features->probe_energie)
    bst_db_beam_set_value (self->lbeam, lprobe->energie);
  if (self->rbeam && rprobe && rprobe->probe_features->probe_energie)
    bst_db_beam_set_value (self->rbeam, rprobe->energie);
  bse_source_queue_probe_request (self->item, 0, 1, 1, 0, 0);
  bse_source_queue_probe_request (self->item, 1, 1, 1, 0, 0);
}

void
bst_bus_editor_set_bus (BstBusEditor *self,
                        SfiProxy      item)
{
  if (item)
    g_return_if_fail (BSE_IS_BUS (item));
  if (self->item)
    {
      bse_proxy_disconnect (self->item,
                            "any_signal::probes", bus_probes_notify, self,
                            NULL);
      bse_proxy_disconnect (self->item,
                            "any-signal", bus_editor_release_item, self,
                            NULL);
      while (self->params)
        gxk_param_destroy (sfi_ring_pop_head (&self->params));
    }
  self->item = item;
  if (self->item)
    {
      GParamSpec *pspec;
      SfiRing *ring;
      bse_proxy_connect (self->item,
                         "signal::release", bus_editor_release_item, self,
                         NULL);
      /* create and hook up volume params & scopes */
      pspec = bse_proxy_get_pspec (self->item, "left-volume-db");
      GxkParam *lvolume = bst_param_new_proxy (pspec, self->item);
      pspec = bse_proxy_get_pspec (self->item, "right-volume-db");
      GxkParam *rvolume = bst_param_new_proxy (pspec, self->item);
      BstDBMeter *dbmeter = gxk_radget_find (self, "db-meter");
      if (dbmeter)
        {
          GtkRange *range = bst_db_meter_get_scale (dbmeter, 0);
          bst_db_scale_hook_up_param (range, lvolume);
          range = bst_db_meter_get_scale (dbmeter, 1);
          bst_db_scale_hook_up_param (range, rvolume);
          self->lbeam = bst_db_meter_get_beam (dbmeter, 0);
          if (self->lbeam)
            bst_db_beam_set_value (self->lbeam, -G_MAXDOUBLE);
          self->rbeam = bst_db_meter_get_beam (dbmeter, 1);
          if (self->rbeam)
            bst_db_beam_set_value (self->rbeam, -G_MAXDOUBLE);
        }
      gxk_radget_add (self, "spinner-box", gxk_param_create_editor (lvolume, "spinner"));
      gxk_radget_add (self, "spinner-box", gxk_param_create_editor (rvolume, "spinner"));
      self->params = sfi_ring_prepend (self->params, lvolume);
      self->params = sfi_ring_prepend (self->params, rvolume);
      /* create remaining params */
      pspec = bse_proxy_get_pspec (self->item, "uname");
      self->params = sfi_ring_prepend (self->params, bst_param_new_proxy (pspec, self->item));
      gxk_radget_add (self, "name-box", gxk_param_create_editor (self->params->data, NULL));
      pspec = bse_proxy_get_pspec (self->item, "inputs");
      self->params = sfi_ring_prepend (self->params, bst_param_new_proxy (pspec, self->item));
      gxk_radget_add (self, "inputs-box", gxk_param_create_editor (self->params->data, NULL));
      /* update params */
      for (ring = self->params; ring; ring = sfi_ring_walk (ring, self->params))
        gxk_param_update (ring->data);
      /* setup scope */
      bse_proxy_connect (self->item,
                         "signal::probes", bus_probes_notify, self,
                         NULL);
      bse_source_queue_probe_request (self->item, 0, 1, 1, 0, 0);
      bse_source_queue_probe_request (self->item, 1, 1, 1, 0, 0);
    }
}

static void
bus_editor_action_exec (gpointer data,
                        gulong   action)
{
  BstBusEditor *self = BST_BUS_EDITOR (data);
  switch (action)
    {
    case ACTION_ADD_BUS:
      break;
    case ACTION_DELETE_BUS:
      break;
    case ACTION_EDIT_BUS:
      break;
    }
  gxk_widget_update_actions_downwards (self);
}

static gboolean
bus_editor_action_check (gpointer data,
                        gulong   action)
{
  // BstBusEditor *self = BST_BUS_EDITOR (data);
  switch (action)
    {
    case ACTION_ADD_BUS:
    case ACTION_DELETE_BUS:
    case ACTION_EDIT_BUS:
      return TRUE;
    default:
      return FALSE;
    }
}

static void
bst_bus_editor_class_init (BstBusEditorClass *class)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (class);
  GtkObjectClass *object_class = GTK_OBJECT_CLASS (class);

  gobject_class->finalize = bst_bus_editor_finalize;
  object_class->destroy = bst_bus_editor_destroy;
}
