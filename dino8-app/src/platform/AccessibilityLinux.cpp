// Real AT-SPI2 accessibility bridge, over D-Bus.
//
// Dear ImGui's docking branch (vendored under third_party/ here) has no
// accessibility hooks at all - no IMGUI_ENABLE_ACCESSIBILITY macro, no
// platform-backend a11y callbacks like the ones Win32/GLFW gained for
// Microsoft's UI Automation years after this branch was cut. There is
// nothing to hang off of ImGui or the GLFW backend, so this talks to the
// system's real accessibility stack directly, from the app side, the same
// way a toolkit's own ATK/AT-SPI bridge would.
//
// AT-SPI2 has no C API for *exporting* a tree (libatspi is a client-side
// convenience library for screen readers *reading* one) - an application
// exposes itself by implementing a handful of D-Bus interfaces
// (org.a11y.atspi.Accessible, .Application, .Text) on its own bus
// connection and registering that connection with the AT-SPI2 registry
// daemon (org.a11y.atspi.Registry) via its Socket.Embed method. That is
// exactly what GNOME's own atk-bridge-2.0 does for every GTK app; this is
// the same protocol, hand-implemented for the regions Dino 8 currently
// mirrors (see docs/ACCESSIBILITY.md for the exact scope and how this was
// verified end-to-end with pyatspi in tests/smoke_accessibility.py).
//
// The registry doesn't live on the ordinary D-Bus session bus - it lives on
// a dedicated "a11y bus" whose address has to be looked up by calling
// org.a11y.Bus.GetAddress on the session bus first (this is exactly what
// at-spi2-registryd itself publishes at /org/a11y/bus, and it's what every
// real assistive-tech-facing app does; there is no shortcut). If the
// session bus is unreachable, or nothing has ever answered org.a11y.Bus
// (no at-spi2-registryd running - the common case on a minimal server or a
// desktop with accessibility never turned on), every function here degrades
// to a silent no-op: the app is not otherwise different from a build
// without this file.
//
// Object model: everything published (the application root, the command
// line, the menu bar and its menus/items, the Layers/Properties panels and
// their rows) is one platform::AccessibleNode tree, replaced wholesale each
// frame by PlatformSetAccessibleTree (see AccessibilityPlatform.h). Since
// which nodes even exist changes frame to frame (a submenu's items only
// exist while it's open; a layer disappears when deleted), objects are
// published under a single g_dbus_connection_register_subtree() handler
// rather than one g_dbus_connection_register_object() per node - GDBus's
// mechanism for "the exact set of child objects is dynamic". Subtree nodes
// are flat (GDBus requires each to be exactly one path segment with no '/'),
// so a node's position in the tree - e.g. root's child 1's child 2's child
// 3 - is encoded as one opaque segment "n1_2_3"; ParseIndices/PathForIndices
// below are the only place that encoding matters.
#if defined(__linux__) && defined(DINO8_HAVE_ATSPI)

#include "platform/AccessibilityPlatform.h"

#include <atspi/atspi-constants.h>
#include <gio/gio.h>

#include <cstdio>
#include <cstdlib>
#include <mutex>
#include <string>
#include <vector>

#ifndef DINO8_VERSION
#define DINO8_VERSION "dev"
#endif

namespace dino8::platform {

namespace {

constexpr const char* kAccessibleIface = "org.a11y.atspi.Accessible";
constexpr const char* kApplicationIface = "org.a11y.atspi.Application";
constexpr const char* kTextIface = "org.a11y.atspi.Text";

// Object paths are only meaningful within our own bus connection (every
// AT-SPI2 app is its own D-Bus connection, so there is no cross-app
// collision to worry about) - kMountPath just has to be internally
// consistent, which is all GetChildren/GetApplication/Parent below rely on.
constexpr const char* kMountPath = "/org/dino8/a11y";
// The standard AT-SPI2 sentinel for "no such object" (a null accessible
// reference) - used for Parent on the root (we don't track the registry's
// own desktop reference) and for an out-of-range GetChildAtIndex.
constexpr const char* kNullPath = "/org/a11y/atspi/null";

const char* const kAccessibleXml = R"XML(
<node>
  <interface name="org.a11y.atspi.Accessible">
    <property name="Name" type="s" access="read"/>
    <property name="Description" type="s" access="read"/>
    <property name="Parent" type="(so)" access="read"/>
    <property name="ChildCount" type="i" access="read"/>
    <property name="Locale" type="s" access="read"/>
    <property name="AccessibleId" type="s" access="read"/>
    <property name="HelpText" type="s" access="read"/>
    <method name="GetChildAtIndex">
      <arg direction="in" name="index" type="i"/>
      <arg direction="out" type="(so)"/>
    </method>
    <method name="GetChildren">
      <arg direction="out" type="a(so)"/>
    </method>
    <method name="GetIndexInParent">
      <arg direction="out" type="i"/>
    </method>
    <method name="GetRelationSet">
      <arg direction="out" type="a(ua(so))"/>
    </method>
    <method name="GetRole">
      <arg direction="out" type="u"/>
    </method>
    <method name="GetRoleName">
      <arg direction="out" type="s"/>
    </method>
    <method name="GetLocalizedRoleName">
      <arg direction="out" type="s"/>
    </method>
    <method name="GetState">
      <arg direction="out" type="au"/>
    </method>
    <method name="GetAttributes">
      <arg direction="out" type="a{ss}"/>
    </method>
    <method name="GetApplication">
      <arg direction="out" type="(so)"/>
    </method>
    <method name="GetInterfaces">
      <arg direction="out" type="as"/>
    </method>
  </interface>
</node>
)XML";

const char* const kApplicationXml = R"XML(
<node>
  <interface name="org.a11y.atspi.Application">
    <property name="ToolkitName" type="s" access="read"/>
    <property name="Version" type="s" access="read"/>
    <property name="AtspiVersion" type="s" access="read"/>
    <property name="Id" type="i" access="read"/>
    <method name="GetLocale">
      <arg direction="in" name="lctype" type="u"/>
      <arg direction="out" type="s"/>
    </method>
  </interface>
</node>
)XML";

const char* const kTextXml = R"XML(
<node>
  <interface name="org.a11y.atspi.Text">
    <property name="CharacterCount" type="i" access="read"/>
    <property name="CaretOffset" type="i" access="read"/>
    <method name="GetText">
      <arg direction="in" name="startOffset" type="i"/>
      <arg direction="in" name="endOffset" type="i"/>
      <arg direction="out" type="s"/>
    </method>
    <method name="GetCharacterCount">
      <arg direction="out" type="i"/>
    </method>
    <method name="SetCaretOffset">
      <arg direction="in" name="offset" type="i"/>
      <arg direction="out" type="b"/>
    </method>
  </interface>
</node>
)XML";

struct Bridge {
  // A private main context, separate from anything the rest of the app
  // uses (GLFW/ImGui here don't run a GLib main loop at all): its only job
  // is dispatching incoming AT-SPI method calls, which we drain explicitly
  // from PumpEvents() - once per frame in the normal case, and also from
  // main.cpp's `waitfile` script directive so queries are still answered
  // while a smoke-test script is deliberately paused (see
  // tests/smoke_accessibility.py).
  GMainContext* ctx = nullptr;
  GDBusConnection* conn = nullptr;
  std::string app_name = "Dino8";
  // Guards top_level: written once per frame from the render thread, read
  // from GDBus method-call/property callbacks. Those callbacks only ever
  // run while the render thread itself is inside g_main_context_iteration
  // (there is no separate GDBus dispatch thread for a connection built on
  // our own private context), so in practice there is never real
  // concurrent access - this exists purely so that invariant is enforced
  // rather than assumed.
  std::mutex tree_mutex;
  std::vector<AccessibleNode> top_level;
  guint reg_subtree = 0;
  GDBusInterfaceInfo* accessible_iface = nullptr;
  GDBusInterfaceInfo* application_iface = nullptr;
  GDBusInterfaceInfo* text_iface = nullptr;
};

Bridge& B() {
  static Bridge bridge;
  return bridge;
}

guint32 AtspiRoleFor(AccessibleRole r) {
  switch (r) {
    case AccessibleRole::Application: return ATSPI_ROLE_APPLICATION;
    case AccessibleRole::Log: return ATSPI_ROLE_LOG;
    case AccessibleRole::MenuBar: return ATSPI_ROLE_MENU_BAR;
    case AccessibleRole::Menu: return ATSPI_ROLE_MENU;
    case AccessibleRole::MenuItem: return ATSPI_ROLE_MENU_ITEM;
    case AccessibleRole::List: return ATSPI_ROLE_LIST;
    case AccessibleRole::ListItem: return ATSPI_ROLE_LIST_ITEM;
  }
  return ATSPI_ROLE_UNKNOWN;
}

const char* AtspiRoleNameFor(AccessibleRole r) {
  switch (r) {
    case AccessibleRole::Application: return "application";
    case AccessibleRole::Log: return "log";
    case AccessibleRole::MenuBar: return "menu bar";
    case AccessibleRole::Menu: return "menu";
    case AccessibleRole::MenuItem: return "menu item";
    case AccessibleRole::List: return "list";
    case AccessibleRole::ListItem: return "list item";
  }
  return "unknown";
}

// Parses the flat "nI_J_K" segment this bridge encodes a node's chain of
// child indices as (see the file comment) back into that index chain. An
// object path equal to kMountPath itself (the application root) parses to
// an empty chain; anything malformed also degrades to the root rather than
// crashing, since a caller with a stale/foreign path is expected here.
std::vector<size_t> ParseIndices(const gchar* object_path) {
  std::vector<size_t> out;
  const std::string mount(kMountPath);
  const std::string p(object_path ? object_path : "");
  if (p.size() <= mount.size() || p.compare(0, mount.size(), mount) != 0) return out;
  const std::string rest = p.substr(mount.size());  // "/n1_2_3"
  if (rest.size() < 2 || rest[0] != '/' || rest[1] != 'n') return out;
  const std::string digits = rest.substr(2);
  size_t pos = 0;
  while (pos <= digits.size()) {
    const size_t next = digits.find('_', pos);
    const std::string tok = digits.substr(pos, next == std::string::npos ? std::string::npos : next - pos);
    if (!tok.empty()) out.push_back(static_cast<size_t>(std::strtoul(tok.c_str(), nullptr, 10)));
    if (next == std::string::npos) break;
    pos = next + 1;
  }
  return out;
}

std::string PathForIndices(const std::vector<size_t>& indices) {
  if (indices.empty()) return kMountPath;
  std::string s = std::string(kMountPath) + "/n";
  for (size_t i = 0; i < indices.size(); ++i) {
    if (i) s += "_";
    s += std::to_string(indices[i]);
  }
  return s;
}

// Walks `indices` (must be non-empty - the empty chain is the root, handled
// separately by every caller) down from the current frame's top-level
// children. Returns nullptr for a chain that no longer resolves (e.g. a
// stale reference into a submenu that has since closed) - callers turn that
// into a normal AT-SPI "no such object" error rather than fabricating data.
const AccessibleNode* ResolveNonRoot(const std::vector<size_t>& indices) {
  if (indices.empty()) return nullptr;
  const std::vector<AccessibleNode>* level = &B().top_level;
  const AccessibleNode* node = nullptr;
  for (size_t idx : indices) {
    if (idx >= level->size()) return nullptr;
    node = &(*level)[idx];
    level = &node->children;
  }
  return node;
}

bool IsRootPath(const gchar* object_path) { return std::string(object_path) == kMountPath; }

void ReturnChildRef(GDBusMethodInvocation* invocation, const std::string& bus_name, const std::string& path) {
  g_dbus_method_invocation_return_value(invocation, g_variant_new("((so))", bus_name.c_str(), path.c_str()));
}

void MethodCall(GDBusConnection*, const gchar*, const gchar* object_path, const gchar* interface_name,
                const gchar* method_name, GVariant* parameters, GDBusMethodInvocation* invocation, gpointer) {
  Bridge& b = B();
  std::lock_guard<std::mutex> lock(b.tree_mutex);
  const std::vector<size_t> indices = ParseIndices(object_path);
  const bool is_root = IsRootPath(object_path);
  const AccessibleNode* node = is_root ? nullptr : ResolveNonRoot(indices);
  if (!is_root && !node) {
    g_dbus_method_invocation_return_error(invocation, G_DBUS_ERROR, G_DBUS_ERROR_UNKNOWN_OBJECT,
                                           "No such accessible object %s", object_path);
    return;
  }
  const AccessibleRole role = is_root ? AccessibleRole::Application : node->role;
  const size_t child_count = is_root ? b.top_level.size() : node->children.size();
  const std::string iface = interface_name;
  const std::string method = method_name;
  const std::string self_name = b.conn ? g_dbus_connection_get_unique_name(b.conn) : "";

  if (iface == kAccessibleIface) {
    if (method == "GetChildAtIndex") {
      gint32 index = -1;
      g_variant_get(parameters, "(i)", &index);
      if (index >= 0 && static_cast<size_t>(index) < child_count) {
        std::vector<size_t> child_indices = indices;
        child_indices.push_back(static_cast<size_t>(index));
        ReturnChildRef(invocation, self_name, PathForIndices(child_indices));
      } else {
        ReturnChildRef(invocation, "", kNullPath);
      }
    } else if (method == "GetChildren") {
      GVariantBuilder builder;
      g_variant_builder_init(&builder, G_VARIANT_TYPE("a(so)"));
      for (size_t i = 0; i < child_count; ++i) {
        std::vector<size_t> child_indices = indices;
        child_indices.push_back(i);
        g_variant_builder_add(&builder, "(so)", self_name.c_str(), PathForIndices(child_indices).c_str());
      }
      g_dbus_method_invocation_return_value(invocation, g_variant_new("(a(so))", &builder));
    } else if (method == "GetIndexInParent") {
      const gint32 idx = is_root ? -1 : static_cast<gint32>(indices.back());
      g_dbus_method_invocation_return_value(invocation, g_variant_new("(i)", idx));
    } else if (method == "GetRelationSet") {
      GVariantBuilder builder;
      g_variant_builder_init(&builder, G_VARIANT_TYPE("a(ua(so))"));
      g_dbus_method_invocation_return_value(invocation, g_variant_new("(a(ua(so)))", &builder));
    } else if (method == "GetRole") {
      g_dbus_method_invocation_return_value(invocation, g_variant_new("(u)", AtspiRoleFor(role)));
    } else if (method == "GetRoleName" || method == "GetLocalizedRoleName") {
      g_dbus_method_invocation_return_value(invocation, g_variant_new("(s)", AtspiRoleNameFor(role)));
    } else if (method == "GetState") {
      // A fuller implementation would report real focus/visible/enabled
      // state bits; returning an empty state set is honest about that gap
      // (see docs/ACCESSIBILITY.md) without risking a wrong bitfield, which
      // some clients treat as authoritative (e.g. "not showing" -> hidden).
      GVariantBuilder builder;
      g_variant_builder_init(&builder, G_VARIANT_TYPE("au"));
      g_variant_builder_add(&builder, "u", 0u);
      g_variant_builder_add(&builder, "u", 0u);
      g_dbus_method_invocation_return_value(invocation, g_variant_new("(au)", &builder));
    } else if (method == "GetAttributes") {
      GVariantBuilder builder;
      g_variant_builder_init(&builder, G_VARIANT_TYPE("a{ss}"));
      g_dbus_method_invocation_return_value(invocation, g_variant_new("(a{ss})", &builder));
    } else if (method == "GetApplication") {
      ReturnChildRef(invocation, self_name, kMountPath);
    } else if (method == "GetInterfaces") {
      GVariantBuilder builder;
      g_variant_builder_init(&builder, G_VARIANT_TYPE("as"));
      g_variant_builder_add(&builder, "s", kAccessibleIface);
      if (role == AccessibleRole::Application) g_variant_builder_add(&builder, "s", kApplicationIface);
      if (role == AccessibleRole::Log) g_variant_builder_add(&builder, "s", kTextIface);
      g_dbus_method_invocation_return_value(invocation, g_variant_new("(as)", &builder));
    } else {
      g_dbus_method_invocation_return_error(invocation, G_DBUS_ERROR, G_DBUS_ERROR_UNKNOWN_METHOD,
                                             "Unknown method %s", method_name);
    }
    return;
  }

  if (iface == kApplicationIface) {
    if (method == "GetLocale") {
      g_dbus_method_invocation_return_value(invocation, g_variant_new("(s)", "C"));
    } else {
      g_dbus_method_invocation_return_error(invocation, G_DBUS_ERROR, G_DBUS_ERROR_UNKNOWN_METHOD,
                                             "Unknown method %s", method_name);
    }
    return;
  }

  if (iface == kTextIface) {
    const std::string text = node ? node->text : std::string();
    if (method == "GetText") {
      gint32 start = 0, end = -1;
      g_variant_get(parameters, "(ii)", &start, &end);
      const gint32 len = static_cast<gint32>(text.size());
      if (start < 0) start = 0;
      if (end < 0 || end > len) end = len;
      if (start > end) start = end;
      const std::string sub = text.substr(static_cast<size_t>(start), static_cast<size_t>(end - start));
      g_dbus_method_invocation_return_value(invocation, g_variant_new("(s)", sub.c_str()));
    } else if (method == "GetCharacterCount") {
      g_dbus_method_invocation_return_value(invocation, g_variant_new("(i)", static_cast<gint32>(text.size())));
    } else if (method == "SetCaretOffset") {
      // Read-only region (there is no caret to move - see
      // docs/ACCESSIBILITY.md); accepted but always reports failure rather
      // than silently pretending to move a caret that does not exist.
      g_dbus_method_invocation_return_value(invocation, g_variant_new("(b)", FALSE));
    } else {
      g_dbus_method_invocation_return_error(invocation, G_DBUS_ERROR, G_DBUS_ERROR_UNKNOWN_METHOD,
                                             "Unknown method %s", method_name);
    }
    return;
  }

  g_dbus_method_invocation_return_error(invocation, G_DBUS_ERROR, G_DBUS_ERROR_UNKNOWN_INTERFACE,
                                         "Unknown interface %s", interface_name);
}

GVariant* GetProperty(GDBusConnection*, const gchar*, const gchar* object_path, const gchar* interface_name,
                       const gchar* property_name, GError** error, gpointer) {
  Bridge& b = B();
  std::lock_guard<std::mutex> lock(b.tree_mutex);
  const std::vector<size_t> indices = ParseIndices(object_path);
  const bool is_root = IsRootPath(object_path);
  const AccessibleNode* node = is_root ? nullptr : ResolveNonRoot(indices);
  if (!is_root && !node) {
    g_set_error(error, G_DBUS_ERROR, G_DBUS_ERROR_UNKNOWN_OBJECT, "No such accessible object %s", object_path);
    return nullptr;
  }
  const AccessibleRole role = is_root ? AccessibleRole::Application : node->role;
  const std::string iface = interface_name;
  const std::string prop = property_name;
  const std::string self_name = b.conn ? g_dbus_connection_get_unique_name(b.conn) : "";

  if (iface == kAccessibleIface) {
    if (prop == "Name") return g_variant_new_string(is_root ? b.app_name.c_str() : node->name.c_str());
    if (prop == "Description") return g_variant_new_string(is_root ? "" : node->description.c_str());
    if (prop == "Parent") {
      if (is_root) return g_variant_new("(so)", "", kNullPath);
      std::vector<size_t> parent_indices = indices;
      parent_indices.pop_back();
      return g_variant_new("(so)", self_name.c_str(), PathForIndices(parent_indices).c_str());
    }
    if (prop == "ChildCount") {
      const size_t n = is_root ? b.top_level.size() : node->children.size();
      return g_variant_new_int32(static_cast<gint32>(n));
    }
    if (prop == "Locale") return g_variant_new_string("C");
    if (prop == "AccessibleId") return g_variant_new_string("");
    if (prop == "HelpText") return g_variant_new_string("");
  } else if (iface == kApplicationIface) {
    if (prop == "ToolkitName") return g_variant_new_string("Dino8-ImGui");
    if (prop == "Version") return g_variant_new_string(DINO8_VERSION);
    if (prop == "AtspiVersion") return g_variant_new_string("2.1");
    if (prop == "Id") return g_variant_new_int32(-1);
  } else if (iface == kTextIface) {
    const std::string text = node ? node->text : std::string();
    if (prop == "CharacterCount") return g_variant_new_int32(static_cast<gint32>(text.size()));
    if (prop == "CaretOffset") return g_variant_new_int32(static_cast<gint32>(text.size()));
  }
  (void)role;
  g_set_error(error, G_DBUS_ERROR, G_DBUS_ERROR_UNKNOWN_PROPERTY, "Unknown property %s", property_name);
  return nullptr;
}

// --- GDBusSubtreeVTable: publishes an arbitrary, per-frame-varying set of
// objects under kMountPath (see the file comment for why a plain
// g_dbus_connection_register_object per node doesn't fit here). -----------

gchar** Enumerate(GDBusConnection*, const gchar*, const gchar*, gpointer) {
  // AT-SPI clients (pyatspi, screen readers) discover children through our
  // own Accessible.GetChildren above, not generic D-Bus introspection, and
  // the subtree is registered with DISPATCH_TO_UNENUMERATED_NODES so an
  // exact enumeration here isn't required for method dispatch to work -
  // this can stay a stub.
  gchar** arr = g_new0(gchar*, 1);
  return arr;
}

GDBusInterfaceInfo* RefIface(GDBusInterfaceInfo* iface) {
  g_dbus_interface_info_ref(iface);
  return iface;
}

GDBusInterfaceInfo** Introspect(GDBusConnection*, const gchar*, const gchar* object_path, const gchar* node,
                                 gpointer) {
  Bridge& b = B();
  std::lock_guard<std::mutex> lock(b.tree_mutex);
  // Despite the API doc's "object_path: the path registered with
  // register_subtree()" phrasing, what's actually passed here for a
  // non-root node is the FULL incoming path (mount + "/" + node) already -
  // node is redundantly just its last segment. ParseIndices wants exactly
  // that full path, so `object_path` alone is already what to parse; do
  // NOT re-append node (that double-counts it, breaking anything past
  // depth 1 - a doubled "n2" is harmless since ParseIndices' strtoul stops
  // at the next '/', but a doubled "n2_0" mis-splits on the extra '_').
  const bool is_root = (node == nullptr);
  const AccessibleNode* n = nullptr;
  if (!is_root) {
    n = ResolveNonRoot(ParseIndices(object_path));
    if (!n) return nullptr;  // no object at this node (e.g. a since-closed submenu) - GDBus reports "unknown"
  }
  const AccessibleRole role = is_root ? AccessibleRole::Application : n->role;

  std::vector<GDBusInterfaceInfo*> ifaces;
  ifaces.push_back(RefIface(b.accessible_iface));
  if (role == AccessibleRole::Application) ifaces.push_back(RefIface(b.application_iface));
  if (role == AccessibleRole::Log) ifaces.push_back(RefIface(b.text_iface));

  GDBusInterfaceInfo** out = g_new0(GDBusInterfaceInfo*, ifaces.size() + 1);
  for (size_t i = 0; i < ifaces.size(); ++i) out[i] = ifaces[i];
  return out;
}

const GDBusInterfaceVTable* Dispatch(GDBusConnection*, const gchar*, const gchar*, const gchar*, const gchar*,
                                      gpointer*, gpointer) {
  static const GDBusInterfaceVTable vtable = {MethodCall, GetProperty, nullptr};
  return &vtable;
}

}  // namespace

void PlatformInitAccessibility(const std::string& app_name) {
  Bridge& b = B();
  if (b.conn) return;  // already initialized
  b.app_name = app_name;

  GError* error = nullptr;
  GDBusConnection* session = g_bus_get_sync(G_BUS_TYPE_SESSION, nullptr, &error);
  if (!session) {
    std::fprintf(stderr, "[accessibility] no D-Bus session bus (%s) - AT-SPI2 accessibility not available\n",
                 error ? error->message : "unknown error");
    if (error) g_error_free(error);
    return;
  }

  GVariant* addr_reply =
      g_dbus_connection_call_sync(session, "org.a11y.Bus", "/org/a11y/bus", "org.a11y.Bus", "GetAddress", nullptr,
                                   G_VARIANT_TYPE("(s)"), G_DBUS_CALL_FLAGS_NONE, -1, nullptr, &error);
  g_object_unref(session);
  if (!addr_reply) {
    std::fprintf(stderr,
                 "[accessibility] AT-SPI2 bus not available (org.a11y.Bus: %s) - is at-spi2-registryd running? "
                 "accessibility not available for this run\n",
                 error ? error->message : "unknown error");
    if (error) g_error_free(error);
    return;
  }
  const gchar* address_c = nullptr;
  g_variant_get(addr_reply, "(&s)", &address_c);
  const std::string a11y_address = address_c ? address_c : "";
  g_variant_unref(addr_reply);

  b.ctx = g_main_context_new();
  g_main_context_push_thread_default(b.ctx);

  b.conn = g_dbus_connection_new_for_address_sync(
      a11y_address.c_str(),
      static_cast<GDBusConnectionFlags>(G_DBUS_CONNECTION_FLAGS_AUTHENTICATION_CLIENT |
                                         G_DBUS_CONNECTION_FLAGS_MESSAGE_BUS_CONNECTION),
      nullptr, nullptr, &error);
  if (!b.conn) {
    std::fprintf(stderr, "[accessibility] could not connect to the AT-SPI2 bus at %s (%s)\n", a11y_address.c_str(),
                 error ? error->message : "unknown error");
    if (error) g_error_free(error);
    g_main_context_pop_thread_default(b.ctx);
    g_main_context_unref(b.ctx);
    b.ctx = nullptr;
    return;
  }

  GDBusNodeInfo* accessible_info = g_dbus_node_info_new_for_xml(kAccessibleXml, &error);
  GDBusNodeInfo* application_info = accessible_info ? g_dbus_node_info_new_for_xml(kApplicationXml, &error) : nullptr;
  GDBusNodeInfo* text_info = application_info ? g_dbus_node_info_new_for_xml(kTextXml, &error) : nullptr;
  if (!accessible_info || !application_info || !text_info) {
    std::fprintf(stderr, "[accessibility] internal error parsing AT-SPI2 interface XML (%s)\n",
                 error ? error->message : "unknown error");
    if (error) g_error_free(error);
    if (accessible_info) g_dbus_node_info_unref(accessible_info);
    if (application_info) g_dbus_node_info_unref(application_info);
    g_object_unref(b.conn);
    b.conn = nullptr;
    g_main_context_pop_thread_default(b.ctx);
    g_main_context_unref(b.ctx);
    b.ctx = nullptr;
    return;
  }

  // Keep our own ref on each interface (Introspect() hands these out for
  // the lifetime of the bridge), independent of the GDBusNodeInfo wrappers
  // we unref right after this.
  b.accessible_iface = RefIface(accessible_info->interfaces[0]);
  b.application_iface = RefIface(application_info->interfaces[0]);
  b.text_iface = RefIface(text_info->interfaces[0]);
  g_dbus_node_info_unref(accessible_info);
  g_dbus_node_info_unref(application_info);
  g_dbus_node_info_unref(text_info);

  static const GDBusSubtreeVTable subtree_vtable = {Enumerate, Introspect, Dispatch};
  b.reg_subtree = g_dbus_connection_register_subtree(b.conn, kMountPath, &subtree_vtable,
                                                       G_DBUS_SUBTREE_FLAGS_DISPATCH_TO_UNENUMERATED_NODES, nullptr,
                                                       nullptr, &error);
  if (!b.reg_subtree) {
    std::fprintf(stderr, "[accessibility] could not register the AT-SPI2 object subtree (%s)\n",
                 error ? error->message : "unknown error");
    if (error) g_error_free(error);
  }

  // Publish ourselves as a child of the AT-SPI2 desktop by handing the
  // registry (bus name, object path) for our own root accessible - the
  // same Socket.Embed call GNOME's atk-bridge-2.0 makes for every GTK app.
  GVariant* embed_reply = g_dbus_connection_call_sync(
      b.conn, "org.a11y.atspi.Registry", "/org/a11y/atspi/accessible/root", "org.a11y.atspi.Socket", "Embed",
      g_variant_new("((so))", g_dbus_connection_get_unique_name(b.conn), kMountPath), G_VARIANT_TYPE("((so))"),
      G_DBUS_CALL_FLAGS_NONE, -1, nullptr, &error);
  if (!embed_reply) {
    std::fprintf(stderr,
                 "[accessibility] AT-SPI2 registry rejected embedding (%s) - our accessible objects are still "
                 "queryable directly by bus name, but won't appear under the AT-SPI desktop\n",
                 error ? error->message : "unknown error");
    if (error) g_error_free(error);
  } else {
    g_variant_unref(embed_reply);
  }

  g_main_context_pop_thread_default(b.ctx);
}

void PlatformSetAccessibleTree(std::vector<AccessibleNode> top_level) {
  Bridge& b = B();
  if (!b.conn) return;
  std::lock_guard<std::mutex> lock(b.tree_mutex);
  b.top_level = std::move(top_level);
}

void PlatformPumpAccessibilityEvents() {
  Bridge& b = B();
  if (!b.ctx) return;
  while (g_main_context_iteration(b.ctx, FALSE)) {
  }
}

void PlatformShutdownAccessibility() {
  Bridge& b = B();
  if (!b.conn) return;
  if (b.reg_subtree) g_dbus_connection_unregister_subtree(b.conn, b.reg_subtree);
  b.reg_subtree = 0;
  if (b.accessible_iface) g_dbus_interface_info_unref(b.accessible_iface);
  if (b.application_iface) g_dbus_interface_info_unref(b.application_iface);
  if (b.text_iface) g_dbus_interface_info_unref(b.text_iface);
  b.accessible_iface = b.application_iface = b.text_iface = nullptr;
  g_object_unref(b.conn);
  b.conn = nullptr;
  if (b.ctx) {
    g_main_context_unref(b.ctx);
    b.ctx = nullptr;
  }
}

}  // namespace dino8::platform

#endif  // defined(__linux__) && defined(DINO8_HAVE_ATSPI)
