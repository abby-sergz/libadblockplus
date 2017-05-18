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
#include <chrono>
#include <thread>

namespace AdblockPlus { namespace Utils {
  std::string Slurp(std::istream& stream);
}}

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
    m_jsEngine = createJsEngine();
    m_latch = JSLatch::InstallLatch(*m_jsEngine);
  }
  virtual JsEnginePtr createJsEngine()
  {
    return JsEngine::New();
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

typedef HeapSnapshotTest HeapSnapshotPatternsTest;
INSTANTIATE_TEST_CASE_P(INIParserStructures,
  HeapSnapshotPatternsTest, ::testing::Values("patterns.ini"));

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
  std::cout << "before taking snapshot" << std::endl;
  std::this_thread::sleep_for(std::chrono::seconds(100));
  std::cout << "taking snapshot" << std::endl;
  WriteHeapSnapshot("done");
  std::cout << "after taking snapshot" << std::endl;
  std::this_thread::sleep_for(std::chrono::seconds(100));
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
    "function processLines(data) {"
    "  for (let i = 0; i < data.length; ++i) {"
    "    let line = data[i];"
    "    let filter = Filter.fromText(line);"
    "    if (filter instanceof RegExpFilter)"
    "      defaultMatcher.add(filter);"
    "    else if (filter instanceof ElemHideBase) {"
    "      if (filter instanceof CSSPropertyFilter) CSSRules.add(filter);"
    "      else ElemHide.add(filter);"
    "    }"
    "  }"
    "};"
    "_fileSystem.readOpt(\"" + m_filterFiles + "\", function(result){"
    "  let data = result.content;"
    "  processLines(data);"
    "  let data_ascii = result.content_ascii;"
    "  processLines(data_ascii);"
    "  unlockLatch();"
    "})");
  m_latch->wait();
  std::cout << "before taking snapshot" << std::endl;
  std::this_thread::sleep_for(std::chrono::seconds(20));
  std::cout << "taking snapshot" << std::endl;
  m_jsEngine->NotifyLowMemory();
  //WriteHeapSnapshot("done");
  std::cout << "after taking snapshot before notifying about low memory" << std::endl;
  std::this_thread::sleep_for(std::chrono::seconds(20));
  std::cout << "second notify" << std::endl;
  m_jsEngine->NotifyLowMemory();
  std::cout << "after low memory notification" << std::endl;
  std::this_thread::sleep_for(std::chrono::seconds(20));
}

TEST_P(HeapSnapshotTest, BuildDomainStats)
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
    "let domainCounter = new Map();"
    "function processLines(data) {"
    "  for (let i = 0; i < data.length; ++i) {"
    "    let line = data[i];"
    "    let filter = Filter.fromText(line);"
    "    let domains = filter.domains;"
    "    if (!domains) continue;"
    "    for (let domain in domains) {"
    "      let prevValue = domainCounter.get(domain);\n"
    "      domainCounter.set(domain, prevValue ? prevValue + 1 : 1);\n"
    "    }"
    "  }"
    "};"
    "_fileSystem.readOpt(\"" + m_filterFiles + "\", function(result){"
    "  let data = result.content;"
    "  processLines(data);"
    "  let data_ascii = result.content_ascii;"
    "  processLines(data_ascii);\n"
    "  console.log(JSON.stringify([...domainCounter]));"
    "  unlockLatch();"
    "})");
  m_latch->wait();
  std::cout << "before taking snapshot" << std::endl;
  std::this_thread::sleep_for(std::chrono::seconds(20));
  std::cout << "taking snapshot" << std::endl;
  m_jsEngine->NotifyLowMemory();
  //WriteHeapSnapshot("done");
  std::cout << "after taking snapshot before notifying about low memory" << std::endl;
  std::this_thread::sleep_for(std::chrono::seconds(20));
  std::cout << "second notify" << std::endl;
  m_jsEngine->NotifyLowMemory();
  std::cout << "after low memory notification" << std::endl;
  std::this_thread::sleep_for(std::chrono::seconds(20));
}
TEST_P(HeapSnapshotPatternsTest, FilterClassesAndMatcherAndIniParser)
{
  WriteHeapSnapshot("fresh");

  evaluateFiles({ "compat.js",
    "coreUtils.js",
    "io.js",
    "events.js",
    "filterNotifier.js",
    "filterClasses.js",
    "subscriptionClasses.js",
    "matcher.js",
    "elemHide.js",
    "cssRules.js",
    "INIParser.js" });

  WriteHeapSnapshot("abp-code");
  m_jsEngine->Evaluate("let {Filter, RegExpFilter, ElemHideBase, CSSPropertyFilter} = require(\"filterClasses\");"
    "let {defaultMatcher} = require(\"matcher\");"
    "let {ElemHide} = require(\"elemHide\");"
    "let {CSSRules} = require(\"cssRules\");"
    "let {INIParser} = require(\"INIParser\");"
    "let {IO} = require(\"io\");"
    "let parser = new INIParser();"
    "IO.readFromFile({ path: \"" + m_filterFiles + "\" }, parser, function(result){"
    "  console.log(result);"
    "  unlockLatch();"
    "})");
  m_latch->wait();
  std::cout << "before taking snapshot" << std::endl;
  std::this_thread::sleep_for(std::chrono::seconds(20));
  std::cout << "taking snapshot" << std::endl;
  m_jsEngine->NotifyLowMemory();
  WriteHeapSnapshot("done");
  std::cout << "after taking snapshot before notifying about low memory" << std::endl;
  std::this_thread::sleep_for(std::chrono::seconds(20));
  std::cout << "second notify" << std::endl;
  m_jsEngine->NotifyLowMemory();
  std::cout << "after low memory notification" << std::endl;
  std::this_thread::sleep_for(std::chrono::seconds(20));
}

namespace {
  class ProfileFileSystem : public AdblockPlus::FileSystem
  {
  public:
    explicit ProfileFileSystem(const AdblockPlus::FileSystemPtr& fs)
    : m_fs(fs)
    , m_writePatternsIniCounter(0)
    {
      m_fakePatternsIniPath = "/sdcard/patterns.ini";
      auto fakePatternsIniStat = m_fs->Stat(m_fakePatternsIniPath);
      if (!fakePatternsIniStat.exists || !fakePatternsIniStat.isFile)
        m_fakePatternsIniPath.clear();
    }
    std::shared_ptr<std::istream> Read(const std::string& path) const override {
      bool useFakePatternsIni = isPatternsIniPath(path) && !m_fakePatternsIniPath.empty();
      return m_fs->Read(useFakePatternsIni ? m_fakePatternsIniPath : path);
    }
    void Write(const std::string& path, std::istream& data) override {
      auto content = AdblockPlus::Utils::Slurp(data);
      if (isPatternsIniPath(path)) {
        std::stringstream ss;
        ss << content;
        m_fs->Write("/sdcard/patterns.ini-write-" + std::to_string(m_writePatternsIniCounter++), ss);
      }
      std::stringstream ss;
      ss << content;
      ss.flush();
      m_fs->Write(path, ss);
    }
    void Move(const std::string& fromPath, const std::string& toPath) override {
      m_fs->Move(fromPath, toPath);
    }
    void Remove(const std::string& path) override {
      m_fs->Remove(path);
    }
    StatResult Stat(const std::string& path) const override {
      return m_fs->Stat(path);
    }
    std::string Resolve(const std::string& path) const override {
      return m_fs->Resolve(path);
    }
  private:
    bool isPatternsIniPath(const std::string& path) const {
      return path.find("patterns.ini") != std::string::npos;
    }
  private:
    AdblockPlus::FileSystemPtr m_fs;
    std::string m_fakePatternsIniPath;
    uint32_t m_writePatternsIniCounter;
  };
}

class FilterEngineProfileTest : public HeapSnapshotTest
{
  typedef HeapSnapshotTest super;
  void SetUp() override {
    super::SetUp();
  }
  JsEnginePtr createJsEngine() override {
    auto jsEngine = super::createJsEngine();
    return jsEngine;
  }
};

