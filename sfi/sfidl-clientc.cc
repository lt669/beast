/* SFI - Synthesis Fusion Kit Interface
 * Copyright (C) 2002 Stefan Westerfeld
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General
 * Public License along with this library; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place, Suite 330,
 * Boston, MA 02111-1307, USA.
 */
#include "sfidl-clientc.h"
#include "sfidl-factory.h"
#include "sfidl-namespace.h"
#include "sfidl-options.h"
#include "sfidl-parser.h"
#include <stdio.h>

using namespace Sfidl;
using namespace std;

void CodeGeneratorClientC::printClassMacros()
{
  for (vector<Class>::const_iterator ci = parser.getClasses().begin(); ci != parser.getClasses().end(); ci++)
    {
      if (parser.fromInclude (ci->name)) continue;

      string macro = makeUpperName (NamespaceHelper::namespaceOf (ci->name)) + "_IS_" +
	makeUpperName (NamespaceHelper::nameOf (ci->name));
      string mname = makeMixedName (ci->name);

      printf ("#define %s(proxy) bse_proxy_is_a ((proxy), \"%s\")\n",
	  macro.c_str(), mname.c_str());
    }
  printf("\n");
}

Method CodeGeneratorClientC::methodWithObject (const Class& c, const Method& method)
{
  Method md;
  md.name = method.name;
  md.result = method.result;

  Param class_as_param;
  class_as_param.name = makeLowerName(c.name) + "_object";
  class_as_param.type = c.name;
  md.params.push_back (class_as_param);

  for (vector<Param>::const_iterator pi = method.params.begin(); pi != method.params.end(); pi++)
    md.params.push_back (*pi);

  return md;
}

void CodeGeneratorClientC::printProcedurePrototypes (PrefixSymbolMode mode)
{
  vector<Class>::const_iterator ci;
  vector<Method>::const_iterator mi;

  for (ci = parser.getClasses().begin(); ci != parser.getClasses().end(); ci++)
    {
      if (parser.fromInclude (ci->name)) continue;

      for (mi = ci->methods.begin(); mi != ci->methods.end(); mi++)
	{
	  if (mode == generatePrefixSymbols)
	    prefix_symbols.push_back (makeLowerName (ci->name + "_" + mi->name));
	  else
	    printProcedure (methodWithObject (*ci, *mi), true, ci->name);
	}
    }
  for (mi = parser.getProcedures().begin(); mi != parser.getProcedures().end(); mi++)
    {
      if (parser.fromInclude (mi->name)) continue;

      if (mode == generatePrefixSymbols)
	prefix_symbols.push_back (makeLowerName (mi->name));
      else
	printProcedure (*mi, true);
    }
}

void CodeGeneratorClientC::printProcedureImpl ()
{
  vector<Class>::const_iterator ci;
  vector<Method>::const_iterator mi;

  for (ci = parser.getClasses().begin(); ci != parser.getClasses().end(); ci++)
    {
      if (parser.fromInclude (ci->name)) continue;

      for (mi = ci->methods.begin(); mi != ci->methods.end(); mi++)
	printProcedure (methodWithObject (*ci, *mi), false, ci->name);
    }
  for (mi = parser.getProcedures().begin(); mi != parser.getProcedures().end(); mi++)
    {
      if (parser.fromInclude (mi->name)) continue;

      printProcedure (*mi, false);
    }
}

bool CodeGeneratorClientC::run()
{
  printf("\n/*-------- begin %s generated code --------*/\n\n\n", options.sfidlName.c_str());

  if (generateHeader)
    {
      /* namespace prefixing for symbols defined by the client */

      prefix_symbols.clear();

      printClientChoiceConverterPrototypes (generatePrefixSymbols);
      printClientRecordMethodPrototypes (generatePrefixSymbols);
      printClientSequenceMethodPrototypes (generatePrefixSymbols);
      printProcedurePrototypes (generatePrefixSymbols);

      if (prefix != "")
	{
	  for (vector<string>::const_iterator pi = prefix_symbols.begin(); pi != prefix_symbols.end(); pi++)
	    printf("#define %s %s_%s\n", pi->c_str(), prefix.c_str(), pi->c_str());
	  printf("\n");
	}

      /* generate the header */

      printClientRecordPrototypes();
      printClientSequencePrototypes();

      printClientChoiceDefinitions();
      printClientRecordDefinitions();
      printClientSequenceDefinitions();

      printClientRecordMethodPrototypes (generateOutput);
      printClientSequenceMethodPrototypes (generateOutput);
      printClientChoiceConverterPrototypes (generateOutput);
      printProcedurePrototypes (generateOutput);

      printClassMacros();
    }

  if (generateSource)
    {
      printf("#include <string.h>\n");

      printClientRecordMethodImpl();
      printClientSequenceMethodImpl();
      printChoiceConverters();
      printProcedureImpl();
    }

  printf("\n/*-------- end %s generated code --------*/\n\n\n", options.sfidlName.c_str());
  return true;
}

OptionVector
CodeGeneratorClientC::getOptions()
{
  OptionVector opts = CodeGeneratorCBase::getOptions();

  opts.push_back (make_pair ("--prefix", true));

  return opts;
}

void
CodeGeneratorClientC::setOption (const string& option, const string& value)
{
  if (option == "--prefix")
    {
      prefix = value;
    }
  else
    {
      CodeGeneratorCBase::setOption (option, value);
    }
}

void
CodeGeneratorClientC::help ()
{
  CodeGeneratorCBase::help();
  fprintf (stderr, " --prefix <prefix>           set the prefix for C functions\n");
}

namespace {

class ClientCFactory : public Factory {
public:
  string option() const	      { return "--client-c"; }
  string description() const  { return "generate client C language binding"; }
  
  CodeGenerator *create (const Parser& parser) const
  {
    return new CodeGeneratorClientC (parser);
  }
} client_c_factory;

}

/* vim:set ts=8 sts=2 sw=2: */
