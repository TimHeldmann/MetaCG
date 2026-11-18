/**
 * File: MergePolicyTest.cpp
 * License: Part of the MetaCG project. Licensed under BSD 3 clause license. See LICENSE.txt file at
 * https://github.com/tudasc/metacg/LICENSE.txt
 */

#include "metacg/Callgraph.h"
#include "metacg/MergePolicy.h"

#include "gtest/gtest.h"

namespace {

using metacg::Callgraph;
using metacg::CgNode;
using metacg::MergeByName;
using metacg::MergeByNameAndOrigin;

const CgNode& singleNode(const Callgraph& cg, const std::string& name) {
  const auto& ids = cg.getAllNodesWithName(name);
  EXPECT_EQ(ids.size(), 1U) << "for " << name;
  return *cg.getNode(ids.at(0));
}

const CgNode* nodeWithOrigin(const Callgraph& cg, const std::string& name, const std::string& origin) {
  for (auto id : cg.getAllNodesWithName(name)) {
    if (cg.getNode(id)->getOrigin() == origin) {
      return cg.getNode(id);
    }
  }
  return nullptr;
}

}  // namespace

TEST(MergeByNameAndOrigin, DefinitionsFromDifferentFilesStaySeparate) {
  Callgraph a;
  a.insert("helper", "/src/a.cpp", false, true);
  Callgraph b;
  b.insert("helper", "/src/b.cpp", false, true);

  a.merge(b, MergeByNameAndOrigin());

  EXPECT_EQ(a.getAllNodesWithName("helper").size(), 2U);
  EXPECT_NE(nodeWithOrigin(a, "helper", "/src/a.cpp"), nullptr);
  EXPECT_NE(nodeWithOrigin(a, "helper", "/src/b.cpp"), nullptr);
}

TEST(MergeByNameAndOrigin, MergeByNameMergesDefinitionsFromDifferentFiles) {
  // Documents the behavior MergeByNameAndOrigin improves on
  Callgraph a;
  a.insert("helper", "/src/a.cpp", false, true);
  Callgraph b;
  b.insert("helper", "/src/b.cpp", false, true);

  a.merge(b, MergeByName());

  EXPECT_EQ(a.getAllNodesWithName("helper").size(), 1U);
}

TEST(MergeByNameAndOrigin, SameOriginIsMerged) {
  // An inline function defined in a header is a definition in every translation unit
  Callgraph a;
  a.insert("inlineFn", "/src/include/h.h", false, true);
  Callgraph b;
  b.insert("inlineFn", "/src/lib/../include/./h.h", false, true);

  a.merge(b, MergeByNameAndOrigin());

  EXPECT_EQ(a.getAllNodesWithName("inlineFn").size(), 1U);
}

TEST(MergeByNameAndOrigin, DeclarationIsMergedWithDefinitionFromOtherFile) {
  Callgraph a;
  a.insert("ext", "/src/include/ext.h", false, false);
  Callgraph b;
  b.insert("ext", "/src/ext.cpp", false, true);

  a.merge(b, MergeByNameAndOrigin());

  const auto& node = singleNode(a, "ext");
  EXPECT_TRUE(node.getHasBody());
  EXPECT_EQ(node.getOrigin(), "/src/ext.cpp");
}

TEST(MergeByNameAndOrigin, DefinitionKeepsItsOriginWhenDeclarationIsMerged) {
  Callgraph a;
  a.insert("ext", "/src/ext.cpp", false, true);
  Callgraph b;
  b.insert("ext", "/src/include/ext.h", false, false);

  a.merge(b, MergeByNameAndOrigin());

  const auto& node = singleNode(a, "ext");
  EXPECT_TRUE(node.getHasBody());
  EXPECT_EQ(node.getOrigin(), "/src/ext.cpp");
}

TEST(MergeByNameAndOrigin, UnknownOriginIsMerged) {
  Callgraph a;
  a.insert("fn", std::nullopt, false, true);
  Callgraph b;
  b.insert("fn", "/src/b.cpp", false, true);
  Callgraph c;
  c.insert("fn", "", false, true);

  a.merge(b, MergeByNameAndOrigin());
  a.merge(c, MergeByNameAndOrigin());

  EXPECT_EQ(a.getAllNodesWithName("fn").size(), 1U);
}

TEST(MergeByNameAndOrigin, PrefersNodeWithSameOrigin) {
  Callgraph a;
  a.insert("helper", "/src/a.cpp", false, true);
  a.insert("helper", "/src/b.cpp", false, true);
  Callgraph b;
  auto& callerB = b.insert("callerB", "/src/b.cpp", false, true);
  auto& helperB = b.insert("helper", "/src/b.cpp", false, true);
  b.addEdge(callerB, helperB);

  a.merge(b, MergeByNameAndOrigin());

  EXPECT_EQ(a.getAllNodesWithName("helper").size(), 2U);
  const auto* mergedHelperB = nodeWithOrigin(a, "helper", "/src/b.cpp");
  ASSERT_NE(mergedHelperB, nullptr);
  EXPECT_TRUE(a.existsEdge(singleNode(a, "callerB"), *mergedHelperB));
}

TEST(MergeByNameAndOrigin, EdgesGoToTheFunctionOfTheirTranslationUnit) {
  // Two translation units each call their own static helper of the same name
  Callgraph a;
  auto& callerA = a.insert("callerA", "/src/a.cpp", false, true);
  auto& helperA = a.insert("helper", "/src/a.cpp", false, true);
  a.addEdge(callerA, helperA);
  Callgraph b;
  auto& callerB = b.insert("callerB", "/src/b.cpp", false, true);
  auto& helperB = b.insert("helper", "/src/b.cpp", false, true);
  b.addEdge(callerB, helperB);

  a.merge(b, MergeByNameAndOrigin());

  const auto* mergedHelperA = nodeWithOrigin(a, "helper", "/src/a.cpp");
  const auto* mergedHelperB = nodeWithOrigin(a, "helper", "/src/b.cpp");
  ASSERT_NE(mergedHelperA, nullptr);
  ASSERT_NE(mergedHelperB, nullptr);
  const auto& mergedCallerA = singleNode(a, "callerA");
  const auto& mergedCallerB = singleNode(a, "callerB");
  EXPECT_TRUE(a.existsEdge(mergedCallerA, *mergedHelperA));
  EXPECT_FALSE(a.existsEdge(mergedCallerA, *mergedHelperB));
  EXPECT_TRUE(a.existsEdge(mergedCallerB, *mergedHelperB));
  EXPECT_FALSE(a.existsEdge(mergedCallerB, *mergedHelperA));
}
