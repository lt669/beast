// Licensed GNU LGPL v2.1 or later: http://www.gnu.org/licenses/lgpl.html
#ifndef __BSE_SUB_SYNTH_H__
#define __BSE_SUB_SYNTH_H__
#include <bse/bsesource.hh>
#define BSE_TYPE_SUB_SYNTH		(BSE_TYPE_ID (BseSubSynth))
#define BSE_SUB_SYNTH(object)		(G_TYPE_CHECK_INSTANCE_CAST ((object), BSE_TYPE_SUB_SYNTH, BseSubSynth))
#define BSE_SUB_SYNTH_CLASS(class)	(G_TYPE_CHECK_CLASS_CAST ((class), BSE_TYPE_SUB_SYNTH, BseSubSynthClass))
#define BSE_IS_SUB_SYNTH(object)	(G_TYPE_CHECK_INSTANCE_TYPE ((object), BSE_TYPE_SUB_SYNTH))
#define BSE_IS_SUB_SYNTH_CLASS(class)	(G_TYPE_CHECK_CLASS_TYPE ((class), BSE_TYPE_SUB_SYNTH))
#define BSE_SUB_SYNTH_GET_CLASS(object)	(G_TYPE_INSTANCE_GET_CLASS ((object), BSE_TYPE_SUB_SYNTH, BseSubSynthClass))

struct BseSubSynth : BseSource {
  BseSNet	  *snet;
  gchar		 **input_ports;
  gchar		 **output_ports;
  guint            midi_channel;
  guint            null_shortcut : 1;
};
struct BseSubSynthClass : BseSourceClass
{};

/// Set whether to pass inputs through to outputs if SNet is unset.
void bse_sub_synth_set_null_shortcut (BseSubSynth *self, bool enabled);
/// Override the @a midi_channel for the SNet (unset override with midi_channel=0).
void bse_sub_synth_set_midi_channel  (BseSubSynth *self, uint midi_channel);

namespace Bse {

class SubSynthImpl : public SourceImpl, public virtual SubSynthIface {
protected:
  virtual  ~SubSynthImpl ();
public:
  explicit  SubSynthImpl (BseObject*);
};

} // Bse

#endif /* __BSE_SUB_SYNTH_H__ */
