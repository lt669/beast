// This Source Code Form is licensed MPL-2.0: http://mozilla.org/MPL/2.0
#include <bse/bsemain.hh>
#include <bse/testing.hh>
#include "bse/internal.hh"

using Bse::printerr;
typedef Bse::IntegrityCheck::TestFunc TestFunc;

// == BSE_INTEGRITY_TEST Registry ==
struct TestEntry {
  TestFunc    test;
  const char *func;
  const char *file;
  int         line;
  TestEntry (const char *_file, int _line, const char *_func, TestFunc _test) :
    test (_test), func (_func), file (_file), line (_line)
  {}
};
static std::vector<TestEntry> *tests = NULL; // NOTE, this must be available for high priority early constructors

// == BSE_INTEGRITY_CHECK Activation ==
namespace Bse {
// Override Bse weak symbol to enable Bse's internal integrity tests, see bcore.hh
const bool IntegrityCheck::enabled = true;
// Registration function called for all integrity tests
void
IntegrityCheck::Test::register_test (const char *file, int line, const char *func, TestFunc test)
{
  if (!tests)
    tests = new std::vector<TestEntry>();
  tests->push_back (TestEntry (file, line, func, test));
}
} // Bse

static int      // for backtrace tests
my_compare_func (const void*, const void*)
{
  BSE_BACKTRACE();
  exit (0);
}


// == Main test program ==
int
main (int argc, char *argv[])
{
  bse_init_test (&argc, argv);
  Bse::set_debug_flags (Bse::DebugFlags::SIGQUIT_ON_ABORT);

  if (argc >= 2 && String ("--backtrace") == argv[1])
    {
      char dummy_array[3] = { 1, 2, 3 };
      qsort (dummy_array, 3, 1, my_compare_func);
    }
  else if (argc >= 2 && String ("--assert_return1") == argv[1])
    {
      assert_return (1, 0);
      return 0;
    }
  else if (argc >= 2 && String ("--assert_return0") == argv[1])
    {
      assert_return (0, 0);
      return 0;
    }
  else if (argc >= 2 && String ("--assert_return_unreached") == argv[1])
    {
      assert_return_unreached (0);
      return 0;
    }
  else if (argc >= 2 && String ("--fatal_error") == argv[1])
    {
      Bse::fatal_error ("got argument --fatal_error");
      return 0;
    }
  else if (argc >= 2 && String ("--return_unless0") == argv[1])
    {
      return_unless (0, 7);
      return 0;
    }
  else if (argc >= 2 && String ("--return_unless1") == argv[1])
    {
      return_unless (1, 8);
      return 0;
    }

  // integrity tests
  assert_return (Bse::IntegrityCheck::checks_enabled() == true, -1);

  if (tests)
    for (const auto &te : *tests)
      { // note, more than one space after "TESTING:" confuses emacs file:line matches
        printerr ("  TESTING: %s:%u: %s…\n", te.file, te.line, te.func);
        te.test();
        printerr ("    …DONE  (%s)\n", te.func);
      }

  return 0;
}
