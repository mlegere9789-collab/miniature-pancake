// Turns Dino Flow values into viewport preview lines/points and, for Bake,
// into real document objects.
#pragma once

#include <string>
#include <vector>

#include "doc/Document.h"
#include "flow/FlowGraph.h"

namespace dino8::flow {

// Appends line-segment and point samples for `v` into the given buffers
// (x,y,z per vertex, matching CommandContext::PreviewLines()'s layout).
void AppendPreviewGeometry(const Value& v, std::vector<float>& lines, std::vector<float>& points);

// Every preview-enabled node's output geometry, gathered for the viewport.
void CollectGraphPreview(Graph& g, std::vector<float>& lines, std::vector<float>& points,
                         std::vector<std::pair<kernel::Point3d, std::string>>& tags);

// Bakes one value into the document as a real object; returns kNoObject on
// failure (a non-geometry value, e.g. a bare number). When `graph_source`
// is non-empty the new object is tagged
//   FlowGraph  = graph_source (the .dflow path the value came from)
//   FlowNode   = source_node (decimal NodeId of the Bake node)
// so GrasshopperUpdateBakes (cmd_flow.cpp) can find and replace it later
// without touching objects the user added by hand or baked from another
// graph. Untagged (graph_source empty) when called from contexts that don't
// track a source graph (kept as the old, always-available signature).
app::ObjectId BakeValue(app::Document& doc, const Value& v, int layer_index, const std::string& name,
                        const std::string& graph_source = "", NodeId source_node = kNoNode);

// Runs every Bake node's inputs into the document, tagging each baked
// object with the graph's file path (Graph::path; untagged when the graph
// was never loaded from / saved to a file) and its source Bake node id -
// see BakeValue above. Returns the number of objects created.
int BakeGraph(Graph& g, app::Document& doc);

// Removes every document object previously baked from `graph_source` (its
// FlowGraph tag) and bakes `g` again, so a re-run of the same .dflow file
// replaces its old output in place instead of piling up duplicates.
// Objects baked from a different graph, or added by hand, are untouched.
// Returns the number of objects created by the fresh bake.
int RebakeGraph(Graph& g, app::Document& doc, const std::string& graph_source);

}  // namespace dino8::flow
