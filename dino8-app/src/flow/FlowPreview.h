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
// failure (a non-geometry value, e.g. a bare number).
app::ObjectId BakeValue(app::Document& doc, const Value& v, int layer_index, const std::string& name);

// Runs every Bake node's inputs into the document. Returns the number of
// objects created.
int BakeGraph(Graph& g, app::Document& doc);

}  // namespace dino8::flow
