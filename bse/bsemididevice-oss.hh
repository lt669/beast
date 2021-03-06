// Licensed GNU LGPL v2.1 or later: http://www.gnu.org/licenses/lgpl.html
#ifndef	__BSE_MIDI_DEVICE_OSS_H__
#define	__BSE_MIDI_DEVICE_OSS_H__

#include	<bse/bsemididevice.hh>


/* --- object type macros --- */
#define BSE_TYPE_MIDI_DEVICE_OSS		(BSE_TYPE_ID (BseMidiDeviceOSS))
#define BSE_MIDI_DEVICE_OSS(object)		(G_TYPE_CHECK_INSTANCE_CAST ((object), BSE_TYPE_MIDI_DEVICE_OSS, BseMidiDeviceOSS))
#define BSE_MIDI_DEVICE_OSS_CLASS(class)	(G_TYPE_CHECK_CLASS_CAST ((class), BSE_TYPE_MIDI_DEVICE_OSS, BseMidiDeviceOSSClass))
#define BSE_IS_MIDI_DEVICE_OSS(object)		(G_TYPE_CHECK_INSTANCE_TYPE ((object), BSE_TYPE_MIDI_DEVICE_OSS))
#define BSE_IS_MIDI_DEVICE_OSS_CLASS(class)	(G_TYPE_CHECK_CLASS_TYPE ((class), BSE_TYPE_MIDI_DEVICE_OSS))
#define BSE_MIDI_DEVICE_OSS_GET_CLASS(object)	(G_TYPE_INSTANCE_GET_CLASS ((object), BSE_TYPE_MIDI_DEVICE_OSS, BseMidiDeviceOSSClass))

struct BseMidiDeviceOSS : BseMidiDevice {
  gchar       *device_name;
};
struct BseMidiDeviceOSSClass : BseMidiDeviceClass
{};

#endif /* __BSE_MIDI_DEVICE_OSS_H__ */
