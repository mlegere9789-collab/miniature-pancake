// Dynamic-block visibility-state instances.
//
// Static blocks (Block/Insert/BlockEdit in cmd_drafting.cpp / cmd_annotate2.cpp)
// stay exactly as they were: a tagged, grouped copy of the definition's
// objects with no per-instance record beyond the "Block"/"BlockInsert" user
// text tags. A *dynamic* block additionally names one or more visibility
// states on its BlockDefinition (BlockDefinition::states) and tags each
// contained object with the states it is visible in (SceneObject::user_text
// key kVisStatesKey, a comma-separated list; an object with no such tag is
// visible in every state, so ordinary un-tagged blocks are unaffected).
//
// A BlockInstance is the small per-placement record a dynamic block needs
// that a static one doesn't: which state it is currently showing and which
// concrete objects were built for it - the same "definition vs. instance"
// split ArchComponents.h didn't need (a Wall has no shared template) but
// blocks do. It is looked up by the instance's Document::Group id, the same
// id the existing tag+group Instance lookups in cmd_annotate2.cpp already
// use, so a dynamic instance is still selectable/explodable exactly like a
// static one.
//
// Persistence follows ArchComponents.h/Constraints.h exactly: the whole
// list round-trips through Document::UserText()["dino8.block_instances"] as
// one JSON array, so it survives Save/Open and Undo/Redo for free (document
// user text is part of the snapshot Document::BeginChange captures).
#pragma once

#include <string>
#include <vector>

#include "doc/Document.h"

namespace dino8::app {

// User-text key (on a BlockDefinition's own SceneObject copies) naming the
// comma-separated visibility states that object is shown in; absent or
// empty means "every state".
constexpr const char* kBlockVisStatesKey = "Dino8.VisStates";

bool ObjectVisibleInState(const SceneObject& def_object, const std::string& state);

struct BlockInstance {
  int group = -1;             // Document::Group id of this instance's objects
  std::string block;          // BlockDefinition::name
  std::string state;          // active visibility state ("" if the block has none)
  kernel::Point3d insert{0, 0, 0};
  std::vector<ObjectId> objects;  // objects currently built for this instance
};

std::vector<BlockInstance> LoadBlockInstances(const Document& doc);
void SaveBlockInstances(Document& doc, const std::vector<BlockInstance>& list);

// Instantiates `name` at `at` filtered to `state` (state visibility applies
// only when the definition has any named states; otherwise every object is
// placed, matching the pre-existing static-block behaviour exactly). When
// the definition has states, also records a BlockInstance so BlockSetState
// can find it again. Returns the new instance's group id, or -1.
int InstantiateDynamicBlock(Document& doc, const std::string& name, kernel::Point3d at, const std::string& state = "");

// Rebuilds one instance in place from its current `state`: deletes its
// previous objects and instantiates a fresh filtered set at the same
// insertion point, updating and persisting the record. Returns false if the
// instance or its definition can no longer be found.
bool RebuildBlockInstance(Document& doc, int group);

// Looks up the stored record for `group`, changes its state and rebuilds.
// Returns false if `group` isn't a known dynamic-block instance.
bool SetBlockInstanceState(Document& doc, int group, const std::string& new_state);

// Finds the BlockInstance owning `object_id` (one of its current objects).
bool FindBlockInstanceByObject(const Document& doc, ObjectId object_id, BlockInstance& out);
bool FindBlockInstanceByGroup(const Document& doc, int group, BlockInstance& out);

}  // namespace dino8::app
