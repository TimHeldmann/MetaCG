/**
 * File: CGProc.cpp
 * License: Part of the MetaCG project. Licensed under BSD 3 clause license. See LICENSE.txt file at
 * https://github.com/tudasc/metacg/LICENSE.txt
 *
 * CGProcessor reads a call graph produced by cgcollector2 (usually after cgmerge2) and materializes edges that were
 * only recorded as metadata:
 *  - override edges: a call to a virtual function may dispatch to any function that (transitively) overrides it
 *  - alias edges: a call through a function pointer may reach any function whose signature matches the pointer type
 */

#include "metacg/config.h"

#include "metacg/LoggerUtil.h"
#include "metacg/io/MCGReader.h"
#include "metacg/io/MCGWriter.h"
// This may appear to be unused, but the variables declared here have side effects on the graph lib
#include "metacg/metadata/BuiltinMD.h"

#include "cgcollector2/SharedDefs.h"
#include "cgcollector2/metadata/exported/AllAliasMetadata.h"
#include "cgcollector2/metadata/exported/FunctionSignatureMetadata.h"

#include "cxxopts.hpp"

#include <fstream>
#include <iostream>
#include <map>
#include <tuple>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

using namespace metacg;

namespace {

/**
 * Adds the edge caller -> callee unless it already exists.
 * @return true if a new edge was added
 */
bool addEdgeIfNew(Callgraph& cg, NodeId caller, NodeId callee) {
  if (cg.existsEdge(caller, callee)) {
    return false;
  }
  return cg.addEdge(caller, callee);
}

/**
 * Collects all functions that override the given function, directly or transitively.
 */
std::vector<NodeId> collectTransitiveOverriders(const Callgraph& cg, NodeId root) {
  std::vector<NodeId> result;
  std::unordered_set<NodeId> visited{root};
  std::vector<NodeId> worklist{root};
  while (!worklist.empty()) {
    const NodeId current = worklist.back();
    worklist.pop_back();
    const auto* node = cg.getNode(current);
    if (!node || !node->has<OverrideMD>()) {
      continue;
    }
    for (const NodeId overrider : node->get<OverrideMD>()->overriddenBy) {
      if (visited.insert(overrider).second) {
        result.push_back(overrider);
        worklist.push_back(overrider);
      }
    }
  }
  return result;
}

/**
 * For every edge caller -> callee where callee is virtual, adds edges from caller to every function that
 * (transitively) overrides callee.
 * @return the number of edges added
 */
size_t materializeOverrideEdges(Callgraph& cg) {
  MCGLogger::logInfo("Materializing override edges");

  // Snapshot the edges, as adding edges while iterating the edge container would invalidate the iterators.
  // Only the original edges are expanded: the override set is already transitively closed, so expanding the
  // newly added edges again would not yield anything new.
  std::vector<std::pair<NodeId, NodeId>> originalEdges;
  originalEdges.reserve(cg.getEdges().size());
  for (const auto& [edge, _] : cg.getEdges()) {
    originalEdges.push_back(edge);
  }

  std::unordered_map<NodeId, std::vector<NodeId>> overriderCache;
  size_t numVirtualFunctions = 0;
  size_t added = 0;
  for (const auto& [caller, callee] : originalEdges) {
    const auto* calleeNode = cg.getNode(callee);
    if (!calleeNode || !calleeNode->has<OverrideMD>()) {
      continue;
    }
    auto it = overriderCache.find(callee);
    if (it == overriderCache.end()) {
      it = overriderCache.emplace(callee, collectTransitiveOverriders(cg, callee)).first;
    }
    for (const NodeId overrider : it->second) {
      if (addEdgeIfNew(cg, caller, overrider)) {
        MCGLogger::logDebug("Override edge: {} -> {} (via {})", cg.getNode(caller)->getFunctionName(),
                            cg.getNode(overrider)->getFunctionName(), calleeNode->getFunctionName());
        ++added;
      }
    }
  }

  for (const auto& node : cg.getNodes()) {
    if (node && node->has<OverrideMD>()) {
      ++numVirtualFunctions;
    }
  }
  if (numVirtualFunctions == 0) {
    MCGLogger::logWarn("No node carries '{}' metadata, no override edges can be materialized", OverrideMD::key);
  }

  MCGLogger::logInfo("Added {} override edges", added);
  return added;
}

/**
 * Signatures are matched on return type, parameter types and implicit object parameter (see
 * FunctionSignature::operator==).
 */
using SignatureKey = std::tuple<std::string, std::vector<std::string>, std::string>;

SignatureKey toKey(const FunctionSignature& sig) { return {sig.retType, sig.paramTypes, sig.thisType}; }

/**
 * For every node with AllAliasMetadata, adds edges to all functions whose FunctionSignatureMetadata matches one of the
 * signatures the node might call through a function pointer.
 * @return the number of edges added
 */
size_t materializeAliasEdges(Callgraph& cg) {
  MCGLogger::logInfo("Materializing function pointer alias edges");

  std::map<SignatureKey, std::vector<NodeId>> signatureToNodes;
  size_t numNodesWithoutSignature = 0;
  for (const auto& node : cg.getNodes()) {
    if (!node) {
      continue;
    }
    if (node->has<FunctionSignatureMetadata>()) {
      signatureToNodes[toKey(node->get<FunctionSignatureMetadata>()->ownSignature)].push_back(node->getId());
    } else {
      // Nodes that represent function pointer variables themselves have no signature, so this is not an error
      MCGLogger::logDebug("{} has no '{}' metadata attached", node->getFunctionName(), FunctionSignatureMetadata::key);
      ++numNodesWithoutSignature;
    }
  }
  if (signatureToNodes.empty()) {
    MCGLogger::logWarn(
        "No node carries '{}' metadata, no alias edges can be materialized. Note that cgcollector2 only exports this "
        "metadata when not run with --whole-program",
        FunctionSignatureMetadata::key);
    return 0;
  }
  MCGLogger::logDebug("{} distinct signatures, {} nodes without signature", signatureToNodes.size(),
                      numNodesWithoutSignature);

  size_t added = 0;
  for (const auto& node : cg.getNodes()) {
    if (!node || !node->has<AllAliasMetadata>()) {
      continue;
    }
    for (const auto& mightCall : node->get<AllAliasMetadata>()->mightCall) {
      const auto it = signatureToNodes.find(toKey(mightCall));
      if (it == signatureToNodes.end()) {
        std::ostringstream stream;
        stream << mightCall;
        MCGLogger::logDebug("{} might call {}, but no function with matching signature exists", node->getFunctionName(),
                            stream.str());
        continue;
      }
      for (const NodeId target : it->second) {
        if (addEdgeIfNew(cg, node->getId(), target)) {
          MCGLogger::logDebug("Alias edge: {} -> {}", node->getFunctionName(), cg.getNode(target)->getFunctionName());
          ++added;
        }
      }
    }
  }

  MCGLogger::logInfo("Added {} alias edges", added);
  return added;
}

}  // namespace

int main(int argc, char** argv) {
  MCGLogger::logInfo("Running metacg::CGProcessor (version {}.{})\nGit revision: {}", MetaCG_VERSION_MAJOR,
                     MetaCG_VERSION_MINOR, MetaCG_GIT_SHA);

  // clang-format off
  cxxopts::Options options("cgproc", "Materializes call edges that cgcollector2 recorded only as metadata");
  options.add_options()
      ("input", "input MetaCG file", cxxopts::value<std::string>())
      ("output", "output MetaCG file", cxxopts::value<std::string>())
      ("override-edges", "add edges from callers of virtual functions to all (transitively) overriding functions, "
                         "based on 'overrideMD'", cxxopts::value<bool>()->default_value("false"))
      ("alias-edges", "add edges to all functions whose signature matches a function pointer a function might call, "
                      "based on 'AllAliasMetadata' and 'FunctionSignatureMetadata'", cxxopts::value<bool>()->default_value("false"))
      ("format-version", "MetaCG format version of the output (default: same as input)", cxxopts::value<int>())
      ("h,help", "Print help");
  // clang-format on

  options.parse_positional({"input", "output"});
  options.positional_help("<input> <output>");

  cxxopts::ParseResult result;
  try {
    result = options.parse(argc, argv);
  } catch (const cxxopts::exceptions::exception& e) {
    MCGLogger::logError("{}", e.what());
    std::cout << options.help() << std::endl;
    return EXIT_FAILURE;
  }

  if (result.count("help")) {
    std::cout << options.help() << std::endl;
    return EXIT_SUCCESS;
  }
  if (!result.count("input") || !result.count("output")) {
    MCGLogger::logError("Both an input and an output file are required");
    std::cout << options.help() << std::endl;
    return EXIT_FAILURE;
  }

  const auto inputFile = result["input"].as<std::string>();
  const auto outputFile = result["output"].as<std::string>();
  const bool overrideEdges = result["override-edges"].as<bool>();
  const bool aliasEdges = result["alias-edges"].as<bool>();

  if (!overrideEdges && !aliasEdges) {
    MCGLogger::logWarn("Neither --override-edges nor --alias-edges was given, the graph is written out unchanged");
  }

  io::FileSource fs(inputFile);
  if (fs.get().is_null()) {
    MCGLogger::logError("Could not read input file: {}", inputFile);
    return EXIT_FAILURE;
  }
  const auto reader = io::createReader(fs);
  if (!reader) {
    MCGLogger::logError("Unsupported MetaCG format version: {}", fs.getFormatVersion());
    return EXIT_FAILURE;
  }
  std::unique_ptr<Callgraph> cg = reader->read();

  // Alias edges first, so the override edges expand them as well: a pointer to member function of class A only
  // matches the member functions declared in A (and its bases), but calling it may dispatch to an override in a class
  // derived from A.
  if (aliasEdges) {
    materializeAliasEdges(*cg);
  }
  if (overrideEdges) {
    materializeOverrideEdges(*cg);
  }

  const int formatVersion =
      result.count("format-version") ? result["format-version"].as<int>() : std::stoi(fs.getFormatVersion());
  const auto writer = io::createWriter(formatVersion);
  if (!writer) {
    MCGLogger::logError("Unable to create a writer for format version {}", formatVersion);
    return EXIT_FAILURE;
  }
  io::JsonSink js;
  writer->write(cg.get(), js);

  std::ofstream out(outputFile);
  if (!out) {
    MCGLogger::logError("Could not open output file: {}", outputFile);
    return EXIT_FAILURE;
  }
  out << js.getJson() << std::endl;
  MCGLogger::logInfo("Written to {}", outputFile);
  return EXIT_SUCCESS;
}
