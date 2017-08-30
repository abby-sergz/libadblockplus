/*
* This file is part of Adblock Plus <https://adblockplus.org/>,
* Copyright (C) 2006-2017 eyeo GmbH
*
* Adblock Plus is free software: you can redistribute it and/or modify
* it under the terms of the GNU General Public License version 3 as
* published by the Free Software Foundation.
*
* Adblock Plus is distributed in the hope that it will be useful,
* but WITHOUT ANY WARRANTY; without even the implied warranty of
* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
* GNU General Public License for more details.
*
* You should have received a copy of the GNU General Public License
* along with Adblock Plus.  If not, see <http://www.gnu.org/licenses/>.
*/

#include <AdblockPlus/Platform.h>
#include <gtest/gtest.h>
#include "BaseJsTest.h"

using namespace AdblockPlus;
namespace {
  void ImmediateExecutorSync(const SchedulerTask& task)
  {
    task();
  }
  class BaseHeapSnapshotTest : public BaseJsTest
  {
  public:
    void SetUp() override
    {
      ThrowingPlatformCreationParameters platformParams;
      platformParams.fileSystem = CreateDefaultFileSystem(ImmediateExecutorSync);
      platform.reset(new Platform{std::move(platformParams)});
    }
  };
}

TEST_F(BaseHeapSnapshotTest, DISABLED_FreshJsEngine)
{
  platform->GetJsEngine().WriteHeapSnapshot("fresh");
}

TEST_F(BaseHeapSnapshotTest, DISABLED_AllocateStringsWithDifferentLength)
{
  JsEngine& jsEngine = platform->GetJsEngine();
  jsEngine.WriteHeapSnapshot("string.fresh");
  jsEngine.Evaluate("content = (function(){"
    "let result = new Array(10000);"
    "let i = 0;"
    "for(;i < result.length; ++i) {"
    "  let sa = new Array(i);"
    "  let j = sa.length;"
    "  while (j-- > 0) sa[j] = \"a\";"
    "  result[i] = sa.join();"
    "}"
    "return result;"
    "})();"
    "emptyString = \"\";");
  jsEngine.WriteHeapSnapshot("strings.done");
}

TEST_F(BaseHeapSnapshotTest, DISABLED_ReadBigTextFileIntoJSString)
{
  JsEngine& jsEngine = platform->GetJsEngine();
  jsEngine.WriteHeapSnapshot("text-file-read.fresh");
  jsEngine.Evaluate("let content; _fileSystem.read(\"easylist.txt\", function(result){"
    "content = result;"
    "})");
  jsEngine.WriteHeapSnapshot("text-file-read.done");
}

TEST_F(BaseHeapSnapshotTest, DISABLED_SplitBigTextFileIntoJSStrings)
{
  std::string fileName = "easylist.txt";
  std::string outputPrefix = "text-file-" + fileName + "-split";
  JsEngine& jsEngine = platform->GetJsEngine();
  jsEngine.WriteHeapSnapshot(outputPrefix + ".fresh");
  jsEngine.Evaluate("_fileSystem.read(\"" + fileName + "\", function(result){"
    "})");
  jsEngine.WriteHeapSnapshot(outputPrefix + ".read-no-saving");
  jsEngine.Evaluate("let content; _fileSystem.read(\"" + fileName + "\", function(result){"
    "  content = result.content;"
    "})");
  jsEngine.WriteHeapSnapshot(outputPrefix + ".read");
  jsEngine.Evaluate("content = content.split(/[\\r\\n]+/);");
  jsEngine.WriteHeapSnapshot(outputPrefix + ".done");
}

#include <iostream>
extern std::string jsSources[];

class HeapSnapshotTest : public BaseHeapSnapshotTest, public ::testing::WithParamInterface<const char*> {
public:
  void SetUp() override {
    m_filterFiles = GetParam();
    const ::testing::TestInfo* const test_info = ::testing::UnitTest::GetInstance()->current_test_info();
    std::string testName = test_info->name();
    for (auto& c : testName) {
      if (c == '/')
        c = '-';
    }
    m_outputPrefix = testName + "-" + m_filterFiles;
    BaseHeapSnapshotTest::SetUp();
  }
protected:
  void waitFor(const std::string& msg, int seconds) {
    if (!m_prevStep.empty())
      std::cout << m_prevStep << " has finished" << std::endl;
    std::cout << "make dump of " << msg << std::endl;
    while (seconds-- > 0)
    {
      std::cout << " " << seconds;
      std::cout.flush();
      std::this_thread::sleep_for(std::chrono::seconds(1));
    }
    std::cout << '\n' << std::endl;
  }

  void WriteHeapSnapshot(const std::string& afterStep) {
    platform->GetJsEngine().WriteHeapSnapshot(m_outputPrefix + "." + afterStep);
  }
  static bool isFileAllowed(const std::vector<std::string>& allowedFiles, const std::string& fileName) {
    return std::find(allowedFiles.begin(), allowedFiles.end(), fileName) != allowedFiles.end();
  }
  void evaluateFiles(const std::vector<std::string>& allowedFiles) {
    auto& jsEngine = platform->GetJsEngine();
    for (int i = 0; !jsSources[i].empty(); i += 2)
    {
      if (!isFileAllowed(allowedFiles, jsSources[i]))
        continue;
      jsEngine.Evaluate(jsSources[i + 1], jsSources[i]);
    }
  }
  std::string m_outputPrefix;
  std::string m_filterFiles;
  std::string m_prevStep;
};

INSTANTIATE_TEST_CASE_P(FilterStructures,
  HeapSnapshotTest, ::testing::Values("easylist.txt", "easylist+aa.txt", "exceptionrules.txt"));

TEST_P(HeapSnapshotTest, DISABLED_FilterClasses)
{
  WriteHeapSnapshot("fresh");

  evaluateFiles({"compat.js", "io.js", "coreUtils.js", "events.js", "filterNotifier.js", "common.js", "filterClasses.js"});

  WriteHeapSnapshot("abp-code");
  auto& jsEngine = platform->GetJsEngine();
  jsEngine.Evaluate(R"js((function(filterFile){
  const {Filter} = require("filterClasses");
  const {IO} = require("io");
  IO.readFromFile(filterFile, function(line) {
    Filter.fromText(line);
  });
});)js").Call(jsEngine.NewValue(m_filterFiles));
  WriteHeapSnapshot("done");
}

TEST_P(HeapSnapshotTest, DISABLED_FilterClassesAndMatcher)
{
  waitFor("fresh", 10);
  WriteHeapSnapshot("fresh");

  evaluateFiles({ "compat.js",
    "io.js",
    "coreUtils.js",
    "events.js",
    "filterNotifier.js",
    "common.js",
    "filterClasses.js",
    "matcher.js",
    "elemHide.js",
    "elemHideEmulation.js" });

  waitFor("js files", 10);
  WriteHeapSnapshot("abp-code");

  auto& jsEngine = platform->GetJsEngine();
  jsEngine.Evaluate(R"js(
  const {Filter, RegExpFilter, ElemHideBase, ElemHideEmulationFilter} = require("filterClasses");
  const {defaultMatcher} = require("matcher");
  const {ElemHideEmulation} = require("elemHideEmulation");
  let lines = [];
  let filters = [];
  let elemHideFilters = [];
(function(filterFile){
  const {IO} = require("io");
  IO.readFromFile(filterFile, function(line) {
      lines.push(line);
    }
  );
}))js").Call(jsEngine.NewValue(m_filterFiles));
  waitFor("lines are read", 10);
  WriteHeapSnapshot("only-lines");
  jsEngine.Evaluate(R"js(
  for (let line of lines)
  {
    filters.push(Filter.fromText(line));
  }
  lines = [];
)js");
  waitFor("filters are created", 10);
  WriteHeapSnapshot("only-filters");
  waitFor("after dump (GC)", 10);
  jsEngine.Evaluate(R"js(
  for (let filter of filters)
  {
    if (filter instanceof RegExpFilter)
      defaultMatcher.add(filter);
    else if (filter instanceof ElemHideBase) {
      if (filter instanceof ElemHideEmulationFilter)
        ElemHideEmulation.add(filter);
      else
       elemHideFilters.push(filter);
    }
  }
  filters = [];
)js");
  waitFor("matcher", 10);
  WriteHeapSnapshot("filters-matcher");
  waitFor("after dump (GC)", 10);
  jsEngine.Evaluate(R"js(
  const {ElemHide} = require("elemHide");
  for (let filter of elemHideFilters)
  {
    ElemHide.add(filter);
  }
  elemHideFilters = [];
)js");
  waitFor("ElemHide.add done", 10);
  jsEngine.NotifyLowMemory();
  waitFor("GC run", 10);
  WriteHeapSnapshot("done");
}
