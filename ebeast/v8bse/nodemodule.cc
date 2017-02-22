// Licensed GNU LGPL v2.1 or later: http://www.gnu.org/licenses/lgpl.html
#include "../../bse/bse.hh"
#include <node.h>
#include <uv.h>
#include <v8pp/class.hpp>
#include <v8pp/convert.hpp>

namespace Aida = Rapicorn::Aida;

// == RemoteHandle Wrapping ==
/* NOTE: A RemoteHandle is a smart pointer to a complex C++ object (possibly behind a thread boundary),
 * so multiple RemoteHandle objects can point to the same C++ object. There are a couple ways that a
 * RemoteHandle can be mapped onto Javascript:
 * 1) A v8::Object contains a RemoteHandle, i.e. every function that returns a new RemoteHandle also
 *    returns a new Object in JS, even if they all point to the same C++ impl. In this case it is
 *    desirable that the JS Object cannot gain new properties (i.e. isSealed() === true).
 *    A major downside here is that two JS Objects that point to the same C++ impl are not === equal.
 * 2) All RemoteHandles that point to the same C++ impl are mapped onto a single JS Object of the
 *    appropriate down-cast type. This correctly provides === equality and new properties added to
 *    a JS Object are preseved whenever another RemoteHandles is mapped into a JS Object that points
 *    to the same C++ impl.
 *    Here, an extra map must be maintained to achieve the (n RemoteHandle) => (1 JS Object) mapping
 *    by storing and looking up the orbid_ that defines the object identity each RemoteHandle points to.
 *    The downside here is resource lockup. Once created, a JS Object must keep its RemoteHandle around
 *    which forces the C++ impl to stay alive. And the v8::Persistent holding the JS Object map entry
 *    must not be weak to prevent GC cycles from "forgetting" the properties stored on the JS Object.
 * 3) A viable middle ground might be using the map from (2) so a JS Object provides correct equality
 *    via ===, but the JS Objects are sealed as in (1) so we can use SetWeak persistents for the map
 *    to avoid resource leaks.
 */

/// Function to wrap a RemoteHandle derived type via v8pp::class_<>::import_external().
typedef v8::Local<v8::Object> (*AidaRemoteHandleWrapper) (v8::Isolate *const, Rapicorn::Aida::RemoteHandle);

/// Default implementation to wrap a RemoteHandle derived type via v8pp::class_<>::import_external().
template<class Native> static v8::Local<v8::Object>
aida_remote_handle_wrapper_impl (v8::Isolate *const isolate, Aida::RemoteHandle rhandle)
{
  Native target = Native::down_cast (rhandle);
  if (target != NULL)
    return v8pp::class_<Native>::import_external (isolate, new Native (target));
  return v8::Local<v8::Object>();
}

/// Map Aida type ids to the corresponding AidaRemoteHandleWrapper.
static AidaRemoteHandleWrapper
aida_remote_handle_wrapper_map (const Aida::TypeHash &thash, AidaRemoteHandleWrapper newfunc)
{
  static std::map<Aida::TypeHash, AidaRemoteHandleWrapper> wmap;
  if (!newfunc)
    return wmap[thash];
  wmap[thash] = newfunc;
  return NULL;
}

/// Create (or find) the corresponding down_cast() JS Object for a RemoteHandle.
static v8::Local<v8::Object>
aida_remote_handle_wrap_native (v8::Isolate *const isolate, Aida::RemoteHandle const &rhandle)
{
  v8::EscapableHandleScope scope (isolate);
  if (NULL != rhandle)
    {
      Aida::TypeHashList thl = rhandle.__aida_typelist__();
      for (const auto &th : thl)
        {
          AidaRemoteHandleWrapper wrapper = aida_remote_handle_wrapper_map (th, AidaRemoteHandleWrapper (NULL));
          if (wrapper)
            return scope.Escape (wrapper (isolate, rhandle));
        }
    }
  return scope.Escape (v8::Local<v8::Object>());
}

/// Retrieve the native RemoteHandle from a JS Object.
template<class NativeClass> static NativeClass&
aida_remote_handle_unwrap_native (v8::Isolate *const isolate, v8::Local<v8::Value> value)
{
  v8::HandleScope scope (isolate);
  NativeClass *nobject = NULL;
  if (!value.IsEmpty() && value->IsObject())
    nobject = v8pp::class_<NativeClass>::unwrap_object (isolate, value);
  if (!nobject)
    throw std::runtime_error ("failed to unwrap C++ Aida::RemoteHandle");
  return *nobject;
}

/// Helper to specialize v8pp::convert<> for all RemoteHandle types.
template<class DerivedHandle>
struct convert_AidaRemoteHandle
{
  using N = DerivedHandle;              // native type, derived from Aida::RemoteHandle
  using J = v8::Local<v8::Object>;      // Javascript type
  static bool is_valid (v8::Isolate *const isolate, v8::Local<v8::Value> v) { return !v.IsEmpty() && v->IsObject(); }
  static N&   from_v8  (v8::Isolate *const isolate, v8::Local<v8::Value> v) { return aida_remote_handle_unwrap_native<N> (isolate, v); }
  static J    to_v8    (v8::Isolate *const isolate, const N &rhandle)       { return aida_remote_handle_wrap_native (isolate, rhandle); }
};

/// Helper for convert_AidaRemoteHandle pointer types.
template<class DerivedHandle>
struct convert_AidaRemoteHandle<DerivedHandle*>
{
  using N = DerivedHandle;              // native type, derived from Aida::RemoteHandle
  using J = v8::Local<v8::Object>;      // Javascript type
  static bool is_valid (v8::Isolate *const isolate, v8::Local<v8::Value> v) { return v8pp::convert<N>::is_valid (isolate, v); }
  static N*   from_v8  (v8::Isolate *const isolate, v8::Local<v8::Value> v) { return &v8pp::convert<N>::from_v8 (isolate, v); }
  static J    to_v8    (v8::Isolate *const isolate, const N *n)             { return !n ? J() : v8pp::convert<N>::to_v8 (isolate, *n); }
};

/// Helper to specialize v8pp::convert<> for all Sequence types.
template<class Class>
struct convert_AidaSequence
{
  using from_type = Class;
  using value_type = typename Class::value_type;
  using to_type = v8::Handle<v8::Array>;
  static bool
  is_valid (v8::Isolate*, v8::Handle<v8::Value> value)
  {
    return !value.IsEmpty() && value->IsArray();
  }
  static from_type
  from_v8 (v8::Isolate *const isolate, v8::Handle<v8::Value> value)
  {
    v8::HandleScope scope (isolate);
    if (!is_valid (isolate, value))
      throw std::invalid_argument ("expected array object");
    v8::Local<v8::Array> arr = value.As<v8::Array>();
    const size_t arrlen = arr->Length();
    from_type result;
    result.reserve (arrlen);
    for (size_t i = 0; i < arrlen; i++)
      result.push_back (v8pp::from_v8<value_type> (isolate, arr->Get (i)));
    return result;
  }
  static to_type
  to_v8 (v8::Isolate *const isolate, from_type const &value)
  {
    v8::EscapableHandleScope scope (isolate);
    v8::Local<v8::Array> arr = v8::Array::New (isolate, value.size());
    for (size_t i = 0; i < value.size(); i++)
      arr->Set (i, v8pp::to_v8 (isolate, value[i]));
    return scope.Escape (arr);
  }
};

#include "v8bse.cc"

// v8pp binding for Bse
static V8stub *bse_v8stub = NULL;

// event loop integration
static uv_poll_t                         bse_uv_watcher;
static Rapicorn::Aida::ClientConnectionP bse_client_connection;
static Bse::ServerH                      bse_server;

// register bindings and start Bse
static void
v8bse_register_module (v8::Local<v8::Object> exports)
{
  assert (bse_v8stub == NULL);
  v8::Isolate *const isolate = v8::Isolate::GetCurrent();
  v8::HandleScope scope (isolate);

  // start Bse
  Bse::String bseoptions = Bse::string_format ("debug-extensions=%d", 0);
  Bse::init_async (NULL, NULL, "BEAST", Bse::string_split (bseoptions, ":"));

  // fetch server handle
  assert (bse_server == NULL);
  assert (bse_client_connection == NULL);
  bse_client_connection = Bse::init_server_connection();
  assert (bse_client_connection != NULL);
  bse_server = Bse::init_server_instance();
  assert (bse_server != NULL);

  // hook BSE connection into libuv event loop
  uv_loop_t *loop = uv_default_loop();
  uv_poll_init (loop, &bse_uv_watcher, bse_client_connection->notify_fd());
  auto bse_uv_callback = [] (uv_poll_t *watcher, int status, int revents) {
    if (bse_client_connection && bse_client_connection->pending())
      bse_client_connection->dispatch();
  };
  uv_poll_start (&bse_uv_watcher, UV_READABLE, bse_uv_callback);

  // hook BSE connection into GLib event loop
  Bse::AidaGlibSource *source = Bse::AidaGlibSource::create (bse_client_connection.get());
  g_source_set_priority (source, G_PRIORITY_DEFAULT);
  g_source_attach (source, g_main_context_default());

  // register v8stub
  v8::Local<v8::Context> context = isolate->GetCurrentContext();
  bse_v8stub = new V8stub (isolate);
  v8::Local<v8::Object> module_instance = bse_v8stub->module_.new_instance();
  v8::Maybe<bool> ok = exports->SetPrototype (context, module_instance);
  assert (ok.FromJust() == true);

  // export server handle
  V8ppType_BseServer &class_ = bse_v8stub->BseServer_class_;
  v8::Local<v8::Object> v8_server = class_.import_external (isolate, new Bse::ServerH (bse_server));
  module_instance->DefineOwnProperty (context, v8pp::to_v8 (isolate, "server"),
                                      v8_server, v8::PropertyAttribute (v8::ReadOnly | v8::DontDelete));

  // debugging aids:
  if (0)
    printerr ("gdb %s %u -ex 'catch catch' -ex 'catch throw'\n", program_invocation_name, Rapicorn::ThisThread::process_pid());
}

// node.js registration
NODE_MODULE (v8bse, v8bse_register_module);
