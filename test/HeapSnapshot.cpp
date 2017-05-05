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

#include <AdblockPlus.h>
#include <gtest/gtest.h>

using namespace AdblockPlus;
namespace {
  // It's very hacky but OK for temporary profiling.
  class JSLatch {
    struct PrivateCtr {};
  public:
    JSLatch(PrivateCtr)
      :m_done(false)
    {
    }
    static std::shared_ptr<JSLatch> InstallLatch(JsEngine& jsEngine)
    {
      auto latch = std::make_shared<JSLatch>(PrivateCtr());
      jsEngine.SetEventCallback("_unlockLatch", [latch](JsValueList&&) {
        latch->set();
      });
      jsEngine.Evaluate("unlockLatch = function(){"
        "_triggerEvent(\"_unlockLatch\");"
      "}");
      return latch;
    }
    void wait() {
      std::unique_lock<std::mutex> lock(m_mutex);
      m_cv.wait(lock, [this]()->bool {
        return m_done;
      });
    }
  private:
    void set() {
      {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_done = true;
      }
      m_cv.notify_one();
    }
  private:
    std::mutex m_mutex;
    std::condition_variable m_cv;
    bool m_done;
  };
}

TEST(HeapSnapshotTest, FreshJsEngine)
{
  JsEnginePtr jsEngine = JsEngine::New();
  jsEngine->WriteHeapSnapshot("fresh");
}

TEST(HeapSnapshotTest, AllocateStringsWithDifferentLength)
{
  JsEnginePtr jsEngine = JsEngine::New();
  jsEngine->WriteHeapSnapshot("string.fresh");
  jsEngine->Evaluate("content = (function(){"
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
  jsEngine->WriteHeapSnapshot("strings.done");
}

TEST(HeapSnapshotTest, ReadBigTextFileIntoJSString)
{
  JsEnginePtr jsEngine = JsEngine::New();
  jsEngine->WriteHeapSnapshot("text-file-read.fresh");
  auto latch = JSLatch::InstallLatch(*jsEngine);
  jsEngine->Evaluate("_fileSystem.read(\"easylist.txt\", function(result){"
    "content = result;"
    "unlockLatch();"
    "})");
  latch->wait();
  jsEngine->WriteHeapSnapshot("text-file-read.done");
}

TEST(HeapSnapshotTest, SplitBigTextFileIntoJSStrings)
{
  std::string fileName = "easylist.txt";
  std::string outputPrefix = "text-file-" + fileName + "-split";
  JsEnginePtr jsEngine = JsEngine::New();
  auto latch = JSLatch::InstallLatch(*jsEngine);
  jsEngine->WriteHeapSnapshot(outputPrefix + ".fresh");
  jsEngine->Evaluate("_fileSystem.read(\"" + fileName + "\", function(result){"
    "  unlockLatch();"
    "})");
  latch->wait();
  latch = JSLatch::InstallLatch(*jsEngine);
  jsEngine->WriteHeapSnapshot(outputPrefix + ".read-no-saving");
  jsEngine->Evaluate("_fileSystem.read(\"" + fileName + "\", function(result){"
    "  content = result.content;"
    "  unlockLatch();"
    "})");
  latch->wait();
  jsEngine->WriteHeapSnapshot(outputPrefix + ".read");
  jsEngine->Evaluate("content = content.split(/[\\r\\n]+/);");
  jsEngine->WriteHeapSnapshot(outputPrefix + ".done");
}

#include <iostream>
extern std::string jsSources[];

class HeapSnapshotTest : public ::testing::TestWithParam<const char*> {
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
    m_jsEngine = JsEngine::New();
    m_latch = JSLatch::InstallLatch(*m_jsEngine);
  }
protected:
  void WriteHeapSnapshot(const std::string& afterStep) {
    m_jsEngine->WriteHeapSnapshot(m_outputPrefix + "." + afterStep);
  }
  static bool isFileAllowed(const std::vector<std::string>& allowedFiles, const std::string& fileName) {
    return std::find(allowedFiles.begin(), allowedFiles.end(), fileName) != allowedFiles.end();
  }
  void evaluateFiles(const std::vector<std::string>& allowedFiles) {
    for (int i = 0; !jsSources[i].empty(); i += 2)
    {
      if (!isFileAllowed(allowedFiles, jsSources[i]))
        continue;
      m_jsEngine->Evaluate(jsSources[i + 1], jsSources[i]);
    }
  }
  std::string m_outputPrefix;
  std::string m_filterFiles;
  JsEnginePtr m_jsEngine;
  std::shared_ptr<JSLatch> m_latch;
};

INSTANTIATE_TEST_CASE_P(FilterStructures,
  HeapSnapshotTest, ::testing::Values("easylist.txt", "easylist+aa.txt", "exceptionrules.txt"));

TEST_P(HeapSnapshotTest, FilterClasses)
{
  WriteHeapSnapshot("fresh");

  evaluateFiles({"compat.js", "coreUtils.js", "events.js", "filterNotifier.js", "filterClasses.js"});

  WriteHeapSnapshot("abp-code");
  m_jsEngine->Evaluate("let {Filter} = require(\"filterClasses\");"
    "_fileSystem.read(\"" + m_filterFiles + "\", function(result){"
    "  let data = result.content.split(/[\\r\\n]+/).slice(1);"
    "  for (let line of data)"
    "    Filter.fromText(line);"
    "  unlockLatch();"
    "})");
  m_latch->wait();
  WriteHeapSnapshot("done");
}

TEST_P(HeapSnapshotTest, FilterClassesAndMatcher)
{
  WriteHeapSnapshot("fresh");

  evaluateFiles({ "compat.js",
    "coreUtils.js",
    "events.js",
    "filterNotifier.js",
    "filterClasses.js",
    "matcher.js",
    "elemHide.js",
    "cssRules.js" });

  WriteHeapSnapshot("abp-code");
  m_jsEngine->Evaluate("let {Filter, RegExpFilter, ElemHideBase, CSSPropertyFilter} = require(\"filterClasses\");"
    "let {defaultMatcher} = require(\"matcher\");"
    "let{ ElemHide } = require(\"elemHide\");"
    "let{ CSSRules } = require(\"cssRules\");"
    "_fileSystem.read(\"" + m_filterFiles + "\", function(result){"
    "  let data = result.content.split(/[\\r\\n]+/).slice(1);"
    "  for (let line of data) {"
    "    let filter = Filter.fromText(line);"
    "    if (filter instanceof RegExpFilter)"
    "      defaultMatcher.add(filter);"
    "    else if (filter instanceof ElemHideBase) {"
    "      if (filter instanceof CSSPropertyFilter) CSSRules.add(filter);"
    "      else ElemHide.add(filter);"
    "    }"
    "  }"
    "  unlockLatch();"
    "})");
  m_latch->wait();
  WriteHeapSnapshot("done");
}