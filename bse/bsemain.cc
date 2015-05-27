// Licensed GNU LGPL v2.1 or later: http://www.gnu.org/licenses/lgpl.html
#include "bsemain.hh"
#include "bsecore.hh"
#include "topconfig.h"
#include "bseserver.hh"
#include "bsesequencer.hh"
#include "bsejanitor.hh"
#include "bseplugin.hh"
#include "bsecategories.hh"
#include "bsemidireceiver.hh"
#include "bsemathsignal.hh"
#include "gsldatacache.hh"
#include "bsepcmdevice.hh"
#include "bsemididevice.hh"
#include "bseengine.hh"
#include "bseblockutils.hh" /* bse_block_impl_name() */
#include <string.h>
#include <stdlib.h>
#include <sys/types.h>
#include <unistd.h>
#include <sfi/sfitests.hh> /* sfti_test_init() */
using namespace Rapicorn;

/* --- prototypes --- */
static void	bse_main_loop		(Rapicorn::AsyncBlockingQueue<int> *init_queue);
static void	bse_async_parse_args	(int *argc_p, char **argv_p, BseMainArgs *margs, const Bse::StringVector &args);
namespace Bse {
static void     init_aida_idl ();
} // Bse

/* --- variables --- */
/* from bse.hh */
const uint		 bse_major_version = BST_MAJOR_VERSION;
const uint		 bse_minor_version = BST_MINOR_VERSION;
const uint		 bse_micro_version = BST_MICRO_VERSION;
const char		*bse_version = BST_VERSION;
GMainContext            *bse_main_context = NULL;
static volatile gboolean bse_initialization_stage = 0;
static gboolean          textdomain_setup = FALSE;
static BseMainArgs       default_main_args = {
  1,                    // n_processors
  64,                   // wave_chunk_padding
  256,                  // wave_chunk_big_pad
  4000,                 // dcache_block_size
  10 * 1024 * 1024,     // dcache_cache_memory
  BSE_KAMMER_NOTE,      // midi_kammer_note (69)
  BSE_KAMMER_FREQUENCY, // kammer_freq (440Hz, historically 435Hz)
  BSE_PATH_BINARIES,    // path_binaries
  NULL,                 // bse_rcfile
  NULL,                 // override_plugin_globs
  NULL,                 // override_script_path
  NULL,			// override_sample_path
  false,                // stand_alone
  true,                 // allow_randomization
  false,                // force_fpu
};
BseMainArgs             *bse_main_args = NULL;

// == BSE Initialization ==
void
bse_init_textdomain_only (void)
{
  bindtextdomain (BSE_GETTEXT_DOMAIN, BST_PATH_LOCALE);
  bind_textdomain_codeset (BSE_GETTEXT_DOMAIN, "UTF-8");
  textdomain_setup = TRUE;
}

const gchar*
bse_gettext (const gchar *text)
{
  g_assert (textdomain_setup == TRUE);
  return dgettext (BSE_GETTEXT_DOMAIN, text);
}

static std::thread async_bse_thread;

void
_bse_init_async (int *argc, char **argv, const char *app_name, const Bse::StringVector &args)
{
  assert (async_bse_thread.get_id() == std::thread::id());      // no async_bse_thread started
  bse_init_textdomain_only();
  if (bse_initialization_stage != 0)
    g_error ("%s() may only be called once", "bse_init_async");
  bse_initialization_stage++;
  if (bse_initialization_stage != 1)
    g_error ("%s() may only be called once", "bse_init_async");
  /* this function is running in the user program and needs to start the main BSE thread */
  /* paranoid assertions */
  g_assert (G_BYTE_ORDER == G_LITTLE_ENDIAN || G_BYTE_ORDER == G_BIG_ENDIAN);
  /* initialize submodules */
  sfi_init (argc, argv, app_name);
  bse_main_args = &default_main_args;
  /* handle argument early*/
  if (argc && argv)
    {
      if (*argc && !g_get_prgname ())
	g_set_prgname (*argv);
      bse_async_parse_args (argc, argv, bse_main_args, args);
    }
  // start main BSE thread
  auto *init_queue = new Rapicorn::AsyncBlockingQueue<int>();
  async_bse_thread = std::thread (bse_main_loop, init_queue);
  // wait for initialization completion of the core thread
  int msg = init_queue->pop();
  assert (msg == 'B');
  delete init_queue;
  async_bse_thread.detach();    // FIXME: rather join on exit
}

const char*
bse_check_version (uint required_major, uint required_minor, uint required_micro)
{
  if (required_major > BST_MAJOR_VERSION)
    return "BSE version too old (major mismatch)";
  if (required_major < BST_MAJOR_VERSION)
    return "BSE version too new (major mismatch)";
  if (required_minor > BST_MINOR_VERSION)
    return "BSE version too old (minor mismatch)";
  if (required_minor < BST_MINOR_VERSION)
    return "BSE version too new (minor mismatch)";
  if (required_micro > BST_MICRO_VERSION)
    return "BSE version too old (micro mismatch)";
  return NULL; // required_micro <= BST_MICRO_VERSION
}

struct AsyncData {
  const gchar *client;
  const std::function<void()> &caller_wakeup;
  Rapicorn::AsyncBlockingQueue<SfiGlueContext*> result_queue;
};

static gboolean
async_create_context (gpointer data)
{
  AsyncData *adata = (AsyncData*) data;
  SfiComPort *port1, *port2;
  sfi_com_port_create_linked ("Client", adata->caller_wakeup, &port1,
			      "Server", bse_main_wakeup, &port2);
  SfiGlueContext *context = sfi_glue_encoder_context (port1);
  bse_janitor_new (port2);
  adata->result_queue.push (context);
  return false; // run-once
}

SfiGlueContext*
_bse_glue_context_create (const char *client, const std::function<void()> &caller_wakeup)
{
  g_return_val_if_fail (client && caller_wakeup, NULL);
  AsyncData adata = { client, caller_wakeup };
  // function runs in user threads and queues handler in BSE thread to create context
  if (bse_initialization_stage < 2)
    g_error ("%s: called without prior %s()", __func__, "Bse::init_async");
  // queue handler to create context
  GSource *source = g_idle_source_new ();
  g_source_set_priority (source, G_PRIORITY_HIGH);
  adata.client = client;
  g_source_set_callback (source, async_create_context, &adata, NULL);
  g_source_attach (source, bse_main_context);
  g_source_unref (source);
  // wake up BSE thread
  g_main_context_wakeup (bse_main_context);
  // receive result asynchronously
  SfiGlueContext *context = adata.result_queue.pop();
  return context;
}

void
bse_main_wakeup ()
{
  g_return_if_fail (bse_main_context != NULL);
  g_main_context_wakeup (bse_main_context);
}

static void
bse_init_core (void)
{
  /* global threading things */
  bse_main_context = g_main_context_new ();
  /* initialize basic components */
  bse_globals_init ();
  _bse_init_signal();
  _bse_init_categories ();
  bse_type_init ();
  bse_cxx_init ();
  /* FIXME: global spawn dir is evil */
  {
    gchar *dir = g_get_current_dir ();
    sfi_com_set_spawn_dir (dir);
    g_free (dir);
  }
  /* initialize GSL components */
  gsl_init ();
  /* remaining BSE components */
  bse_plugin_init_builtins ();
  /* initialize C wrappers around C++ generated types */
  _bse_init_c_wrappers ();
  /* make sure the server is alive */
  bse_server_get ();
  /* load drivers early */
  if (bse_main_args->load_drivers_early)
    {
      SfiRing *ring = bse_plugin_path_list_files (TRUE, FALSE);
      while (ring)
        {
          gchar *name = (char*) sfi_ring_pop_head (&ring);
          const char *error = bse_plugin_check_load (name);
          if (error)
            sfi_diag ("while loading \"%s\": %s", name, error);
          g_free (name);
        }
    }

  /* dump device list */
  if (bse_main_args->dump_driver_list)
    {
      g_printerr ("%s", _("\nAvailable PCM drivers:\n"));
      bse_device_dump_list (BSE_TYPE_PCM_DEVICE, "  ", TRUE, NULL, NULL);
      g_printerr ("%s", _("\nAvailable MIDI drivers:\n"));
      bse_device_dump_list (BSE_TYPE_MIDI_DEVICE, "  ", TRUE, NULL, NULL);
    }
}

static gboolean single_thread_registration_done = FALSE;

static void
server_registration (SfiProxy            server,
                     BseRegistrationType rtype,
                     const gchar        *what,
                     const gchar        *error,
                     gpointer            data)
{
  // BseRegistrationType rtype = bse_registration_type_from_choice (rchoice);
  if (rtype == BSE_REGISTER_DONE)
    single_thread_registration_done = TRUE;
  else
    {
      if (error && error[0])
        sfi_diag ("failed to register \"%s\": %s", what, error);
    }
}

static void
bse_init_intern (int *argc, char **argv, const char *app_name, const Bse::StringVector &args, bool as_test)
{
  bse_init_textdomain_only();

  if (bse_initialization_stage != 0)
    g_error ("%s() may only be called once", "bse_init_intern");
  bse_initialization_stage++;
  if (bse_initialization_stage != 1)
    g_error ("%s() may only be called once", "bse_init_intern");

  /* paranoid assertions */
  g_assert (G_BYTE_ORDER == G_LITTLE_ENDIAN || G_BYTE_ORDER == G_BIG_ENDIAN);

  /* initialize submodules */
  if (as_test)
    sfi_init_test (argc, argv);
  else
    sfi_init (argc, argv, app_name);
  bse_main_args = &default_main_args;
  /* early argument handling */
  if (argc && argv)
    {
      if (*argc && !g_get_prgname ())
	g_set_prgname (*argv);
      bse_async_parse_args (argc, argv, bse_main_args, args);
    }

  bse_init_core ();

  /* initialize core plugins & scripts */
  if (bse_main_args->load_core_plugins || bse_main_args->load_core_scripts)
      g_object_connect (bse_server_get(), "signal::registration", server_registration, NULL, NULL);
  if (bse_main_args->load_core_plugins)
    {
      g_object_connect (bse_server_get(), "signal::registration", server_registration, NULL, NULL);
      SfiRing *ring = bse_plugin_path_list_files (!bse_main_args->load_drivers_early, TRUE);
      while (ring)
        {
          gchar *name = (char*) sfi_ring_pop_head (&ring);
          const char *error = bse_plugin_check_load (name);
          if (error)
            sfi_diag ("while loading \"%s\": %s", name, error);
          g_free (name);
        }
    }
  if (bse_main_args->load_core_scripts)
    {
      BseErrorType error = bse_item_exec (bse_server_get(), "register-scripts", NULL);
      if (error)
        sfi_diag ("during script registration: %s", bse_error_blurb (error));
      while (!single_thread_registration_done)
        {
          g_main_context_iteration (bse_main_context, TRUE);
          // sfi_glue_gc_run ();
        }
    }
  if (as_test)
    {
      StringVector sv = Rapicorn::string_split (Rapicorn::cpu_info(), " ");
      String machine = sv.size() >= 2 ? sv[1] : "Unknown";
      TMSG ("  NOTE   Running on: %s+%s", machine.c_str(), bse_block_impl_name());
    }
  // sfi_glue_gc_run ();
}

void
bse_init_inprocess (int *argc, char **argv, const char *app_name, const Bse::StringVector &args)
{
  bse_init_intern (argc, argv, app_name, args, false);
}

void
bse_init_test (int *argc, char **argv, const Bse::StringVector &args)
{
  bse_init_intern (argc, argv, NULL, args, true);
}

static void
bse_main_loop (Rapicorn::AsyncBlockingQueue<int> *init_queue)
{
  Bse::TaskRegistry::add ("BSE Core", Rapicorn::ThisThread::process_pid(), Rapicorn::ThisThread::thread_pid());
  bse_init_core ();
  // start other threads
  struct Internal : Bse::Sequencer { using Bse::Sequencer::_init_threaded; };
  Internal::_init_threaded();
  // allow aida IDL remoting
  Bse::init_aida_idl();
  // complete initialization
  bse_initialization_stage++;   // = 2
  init_queue->push ('B');       // signal completion to caller
  init_queue = NULL;            // completion invalidates init_queue
  // Bse Core Event Loop
  while (true)                  // FIXME: missing exit handler
    {
      g_main_context_pending (bse_main_context);
      g_main_context_iteration (bse_main_context, TRUE);
    }
  Bse::TaskRegistry::remove (Rapicorn::ThisThread::thread_pid());
}

static guint
get_n_processors (void)
{
#ifdef _SC_NPROCESSORS_ONLN
    gint n = sysconf (_SC_NPROCESSORS_ONLN);
    if (n > 0)
      return n;
#endif
  return 1;
}

static bool
parse_bool_option (const String &s, const char *arg, bool *boolp)
{
  const size_t length = strlen (arg);
  if (s.size() > length && s[length] == '=' && strncmp (&s[0], arg, length) == 0)
    {
      *boolp = string_to_bool (s.substr (length + 1));
      return true;
    }
  return false;
}

static bool
parse_int_option (const String &s, const char *arg, int64 *ip)
{
  const size_t length = strlen (arg);
  if (s.size() > length && s[length] == '=' && strncmp (&s[0], arg, length) == 0)
    {
      *ip = string_to_int (s.substr (length + 1));
      return true;
    }
  return false;
}

static bool
parse_float_option (const String &s, const char *arg, double *fp)
{
  const size_t length = strlen (arg);
  if (s.size() > length && s[length] == '=' && strncmp (&s[0], arg, length) == 0)
    {
      *fp = string_to_float (s.substr (length + 1));
      return true;
    }
  return false;
}

static void
bse_async_parse_args (int *argc_p, char **argv_p, BseMainArgs *margs, const Bse::StringVector &args)
{
  uint argc = *argc_p;
  char **argv = argv_p;
  /* this function is called before the main BSE thread is started,
   * so we can't use any BSE functions yet.
   */
  guint i;
  for (i = 1; i < argc; i++)
    {
      if (strcmp (argv[i], "--g-fatal-warnings") == 0)
	{
	  GLogLevelFlags fatal_mask = (GLogLevelFlags) g_log_set_always_fatal ((GLogLevelFlags) G_LOG_FATAL_MASK);
	  fatal_mask = (GLogLevelFlags) (fatal_mask | G_LOG_LEVEL_WARNING | G_LOG_LEVEL_CRITICAL);
	  g_log_set_always_fatal (fatal_mask);
	  argv[i] = NULL;
	}
      else if (strcmp ("--bse-latency", argv[i]) == 0 ||
	       strncmp ("--bse-latency=", argv[i], 14) == 0)
	{
	  gchar *equal = argv[i] + 13;
	  if (*equal == '=')
            margs->latency = g_ascii_strtoull (equal + 1, NULL, 10);
	  else if (i + 1 < argc)
	    {
	      argv[i++] = NULL;
              margs->latency = g_ascii_strtoull (argv[i], NULL, 10);
	    }
	  argv[i] = NULL;
	}
      else if (strcmp ("--bse-mixing-freq", argv[i]) == 0 ||
	       strncmp ("--bse-mixing-freq=", argv[i], 18) == 0)
	{
	  gchar *equal = argv[i] + 17;
	  if (*equal == '=')
            margs->mixing_freq = g_ascii_strtoull (equal + 1, NULL, 10);
	  else if (i + 1 < argc)
	    {
	      argv[i++] = NULL;
              margs->mixing_freq = g_ascii_strtoull (argv[i], NULL, 10);
	    }
	  argv[i] = NULL;
	}
      else if (strcmp ("--bse-control-freq", argv[i]) == 0 ||
	       strncmp ("--bse-control-freq=", argv[i], 19) == 0)
	{
	  gchar *equal = argv[i] + 18;
	  if (*equal == '=')
            margs->control_freq = g_ascii_strtoull (equal + 1, NULL, 10);
	  else if (i + 1 < argc)
	    {
	      argv[i++] = NULL;
              margs->control_freq = g_ascii_strtoull (argv[i], NULL, 10);
	    }
	  argv[i] = NULL;
	}
      else if (strcmp ("--bse-driver-list", argv[i]) == 0)
	{
          margs->load_drivers_early = TRUE;
          margs->dump_driver_list = TRUE;
	  argv[i] = NULL;
	}
      else if (strcmp ("--bse-pcm-driver", argv[i]) == 0)
	{
          if (i + 1 < argc)
	    {
	      argv[i++] = NULL;
              margs->pcm_drivers = sfi_ring_append (margs->pcm_drivers, argv[i]);
	    }
	  argv[i] = NULL;
	}
      else if (strcmp ("--bse-midi-driver", argv[i]) == 0)
	{
          if (i + 1 < argc)
	    {
	      argv[i++] = NULL;
              margs->midi_drivers = sfi_ring_append (margs->midi_drivers, argv[i]);
	    }
	  argv[i] = NULL;
	}
      else if (strcmp ("--bse-override-plugin-globs", argv[i]) == 0 && i + 1 < argc)
	{
          argv[i++] = NULL;
          margs->override_plugin_globs = argv[i];
	  argv[i] = NULL;
	}
      else if (strcmp ("--bse-override-script-path", argv[i]) == 0 && i + 1 < argc)
	{
          argv[i++] = NULL;
          margs->override_script_path = argv[i];
	  argv[i] = NULL;
	}
      else if (strcmp ("--bse-override-sample-path", argv[i]) == 0 && i + 1 < argc)
	{
	  argv[i++] = NULL;
	  margs->override_sample_path = argv[i];
	  argv[i] = NULL;
	}
      else if (strcmp ("--bse-rcfile", argv[i]) == 0 && i + 1 < argc)
	{
          argv[i++] = NULL;
          g_free ((char*) margs->bse_rcfile);
          margs->bse_rcfile = g_strdup (argv[i]);
	  argv[i] = NULL;
	}
#if 0
      else if (strcmp ("--bse-override-binaries-path", argv[i]) == 0 && i + 1 < argc)
	{
          argv[i++] = NULL;
          margs->path_binaries = argv[i];
	  argv[i] = NULL;
	}
#endif
      else if (strcmp ("--bse-force-fpu", argv[i]) == 0)
	{
          margs->force_fpu = TRUE;
	  argv[i] = NULL;
	}
      else if (strcmp ("--bse-disable-randomization", argv[i]) == 0)
	{
          margs->allow_randomization = FALSE;
	  argv[i] = NULL;
	}
      else if (strcmp ("--bse-enable-randomization", argv[i]) == 0)
	{
          margs->allow_randomization = TRUE;
	  argv[i] = NULL;
	}
    }

  if (!margs->bse_rcfile)
    margs->bse_rcfile = g_strconcat (g_get_home_dir (), "/.bserc", NULL);

  guint e = 1;
  for (i = 1; i < argc; i++)
    if (argv[i])
      {
        argv[e++] = argv[i];
        if (i >= e)
          argv[i] = NULL;
      }
  *argc_p = e;
  for (auto arg : args)
    {
      bool b; double d; int64 i;
      if      (parse_bool_option (arg, "stand-alone", &b))
        margs->stand_alone |= b;
      else if (parse_bool_option (arg, "allow-randomization", &b))
        margs->allow_randomization |= b;
      else if (parse_bool_option (arg, "force-fpu", &b))
        margs->force_fpu |= b;
      else if (parse_bool_option (arg, "load-core-plugins", &b))
        margs->load_core_plugins |= b;
      else if (parse_bool_option (arg, "load-core-scripts", &b))
        margs->load_core_scripts |= b;
      else if (parse_bool_option (arg, "debug-extensions", &b))
        margs->debug_extensions |= b;
      else if (parse_int_option (arg, "wave-chunk-padding", &i))
        margs->wave_chunk_padding = i;
      else if (parse_int_option (arg, "wave-chunk-big-pad", &i))
        margs->wave_chunk_big_pad = i;
      else if (parse_int_option (arg, "dcache-cache-memory", &i))
        margs->dcache_cache_memory = i;
      else if (parse_int_option (arg, "dcache-block-size", &i))
        margs->dcache_block_size = i;
      else if (parse_int_option (arg, "midi-kammer-note", &i))
        margs->midi_kammer_note = i;
      else if (parse_float_option (arg, "kammer-freq", &d))
        margs->kammer_freq = d;
    }

  /* constrain (user) config */
  margs->wave_chunk_padding = MAX (1, margs->wave_chunk_padding);
  margs->wave_chunk_big_pad = MAX (2 * margs->wave_chunk_padding, margs->wave_chunk_big_pad);
  margs->dcache_block_size = MAX (2 * margs->wave_chunk_big_pad + sizeof (((GslDataCacheNode*) NULL)->data[0]), margs->dcache_block_size);
  margs->dcache_block_size = sfi_alloc_upper_power2 (margs->dcache_block_size - 1);
  /* margs->dcache_cache_memory = sfi_alloc_upper_power2 (margs->dcache_cache_memory); */

  /* non-configurable config updates */
  margs->n_processors = get_n_processors ();
}

namespace Bse {

static void
init_aida_idl ()
{
  // setup Aida server connection, so ServerIface::__aida_connection__() yields non-NULL
  Aida::ObjectBroker::bind<Bse::ServerIface> ("inproc://BSE-" BST_VERSION,
                                              shared_ptr_cast<Bse::ServerIface> (&Bse::ServerImpl::instance()));
  // hook up server connection to main loop to process remote calls
  AidaGlibSource *source = AidaGlibSource::create (Bse::ServerIface::__aida_connection__());
  g_source_set_priority (source, BSE_PRIORITY_GLUE);
  g_source_attach (source, bse_main_context);
}

} // Bse
