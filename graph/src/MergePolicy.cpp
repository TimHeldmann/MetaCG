/**
 * File: MergePolicy.cpp
 * License: Part of the MetaCG project. Licensed under BSD 3 clause license. See LICENSE.txt file at
 * https://github.com/tudasc/metacg/LICENSE.txt
 */
#include "metacg/MergePolicy.h"
#include "metacg/Callgraph.h"
#include "metacg/LoggerUtil.h"

#include <filesystem>
#include <vector>

namespace metacg {

std::optional<MergeAction> MergeByName::findMatchingNode(const Callgraph& targetCG, const CgNode& sourceNode) const {
  auto& nameMatches = targetCG.getAllNodesWithName(sourceNode.getFunctionName());
  if (nameMatches.empty()) {
    // There is no matching node: simply copy the source node into the target graph
    return {};
  }
  if (nameMatches.size() > 1) {
    // There is more than one node: in V2, this can't happen. Print a warning and merge with the first node.
    MCGLogger::logWarn("Found multiple nodes with matching name '{}'! Merging with the first node.",
                       sourceNode.getFunctionName());
  }
  auto* targetNode = targetCG.getNode(nameMatches.front());
  assert(targetNode && "Node must not be null");

  // If the source node has no body (i.e. no definition) OR the target node has one, keep the target node.
  // If both functions have bodies: assume inline with identical function bodies.
  if (targetNode->getHasBody() || !sourceNode.getHasBody()) {
    return MergeAction(targetNode->getId(), false);
  }

  return MergeAction(targetNode->getId(), true);
}

namespace {

/// The origin in a canonical form, so that different spellings of the same path compare equal (e.g. "a/../b.h" and
/// "b.h"). This is purely lexical: relative and absolute paths of the same file, or symbolic links, are not unified.
std::optional<std::string> normalizedOrigin(const CgNode& node) {
  auto origin = node.getOrigin();
  if (!origin || origin->empty()) {
    return {};
  }
  return std::filesystem::path(*origin).lexically_normal().generic_string();
}

}  // namespace

std::optional<MergeAction> MergeByNameAndOrigin::findMatchingNode(const Callgraph& targetCG,
                                                                  const CgNode& sourceNode) const {
  const auto& nameMatches = targetCG.getAllNodesWithName(sourceNode.getFunctionName());
  if (nameMatches.empty()) {
    // There is no matching node: simply copy the source node into the target graph
    return {};
  }

  const auto sourceOrigin = normalizedOrigin(sourceNode);

  // Nodes with the same origin are the same function.
  std::vector<NodeId> sameOrigin;
  // Nodes that may be the same function, as the origins can not tell them apart:
  //  - the origin of one of the nodes is unknown, or
  //  - one of the nodes has no body: the origin of a declaration is the file it is declared in (e.g. a header), which
  //    may differ from the file of the definition.
  // Two definitions from different files are different functions. As a function with external linkage has a single
  // definition (inline functions and templates are defined in the same header in every translation unit), these are
  // functions with internal linkage (static functions, anonymous namespaces) that share a name across translation units.
  std::vector<NodeId> compatible;
  for (const auto id : nameMatches) {
    const auto* targetNode = targetCG.getNode(id);
    assert(targetNode && "Node must not be null");
    const auto targetOrigin = normalizedOrigin(*targetNode);
    if (sourceOrigin && targetOrigin && *sourceOrigin == *targetOrigin) {
      sameOrigin.push_back(id);
    } else if (!sourceOrigin || !targetOrigin || !sourceNode.getHasBody() || !targetNode->getHasBody()) {
      compatible.push_back(id);
    }
  }

  const auto& candidates = sameOrigin.empty() ? compatible : sameOrigin;
  if (candidates.empty()) {
    // All nodes with this name are definitions from other files: copy the source node into the target graph
    return {};
  }
  if (candidates.size() > 1) {
    // The origins do not suffice to tell the nodes apart, e.g. a declaration and definitions of functions with internal
    // linkage from several files. Merge with the first node.
    MCGLogger::logWarn("Found multiple nodes with name '{}' that match origin '{}'! Merging with the first node.",
                       sourceNode.getFunctionName(), sourceOrigin.value_or("<unknown>"));
  }

  auto* targetNode = targetCG.getNode(candidates.front());
  assert(targetNode && "Node must not be null");

  // If the source node has no body (i.e. no definition) OR the target node has one, keep the target node.
  // If both functions have bodies: assume inline with identical function bodies.
  if (targetNode->getHasBody() || !sourceNode.getHasBody()) {
    return MergeAction(targetNode->getId(), false);
  }

  return MergeAction(targetNode->getId(), true);
}

}  // namespace metacg
