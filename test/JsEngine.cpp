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

#include <stdexcept>
#include "BaseJsTest.h"

using namespace AdblockPlus;

namespace
{
  class JsEngineTest : public BaseJsTest
  {
  };
}

TEST_F(JsEngineTest, Evaluate)
{
  jsEngine->Evaluate("function hello() { return 'Hello'; }");
  auto result = jsEngine->Evaluate("hello()");
  ASSERT_TRUE(result.IsString());
  ASSERT_EQ("Hello", result.AsString());
}

TEST_F(JsEngineTest, RuntimeExceptionIsThrown)
{
  ASSERT_THROW(jsEngine->Evaluate("doesnotexist()"), std::runtime_error);
}

TEST_F(JsEngineTest, CompileTimeExceptionIsThrown)
{
  ASSERT_THROW(jsEngine->Evaluate("'foo'bar'"), std::runtime_error);
}

TEST_F(JsEngineTest, ValueCreation)
{
  auto value = jsEngine->NewValue("foo");
  ASSERT_TRUE(value.IsString());
  ASSERT_EQ("foo", value.AsString());

  value = jsEngine->NewValue(12345678901234);
  ASSERT_TRUE(value.IsNumber());
  ASSERT_EQ(12345678901234, value.AsInt());

  value = jsEngine->NewValue(true);
  ASSERT_TRUE(value.IsBool());
  ASSERT_TRUE(value.AsBool());

  value = jsEngine->NewObject();
  ASSERT_TRUE(value.IsObject());
  ASSERT_EQ(0u, value.GetOwnPropertyNames().size());
}

namespace {

  bool IsSame(AdblockPlus::JsEngine& jsEngine,
              const AdblockPlus::JsValue& v1, const AdblockPlus::JsValue& v2)
  {
    AdblockPlus::JsValueList params;
    params.push_back(v1);
    params.push_back(v2);
    return jsEngine.Evaluate("f = function(a, b) { return a == b };").Call(params).AsBool();
  }

}

TEST_F(JsEngineTest, ValueCopy)
{
  {
    auto value = jsEngine->NewValue("foo");
    ASSERT_TRUE(value.IsString());
    ASSERT_EQ("foo", value.AsString());

    AdblockPlus::JsValue value2(value);
    ASSERT_TRUE(value2.IsString());
    ASSERT_EQ("foo", value2.AsString());

    ASSERT_TRUE(IsSame(*jsEngine, value, value2));
  }
  {
    auto value = jsEngine->NewValue(12345678901234);
    ASSERT_TRUE(value.IsNumber());
    ASSERT_EQ(12345678901234, value.AsInt());

    AdblockPlus::JsValue value2(value);
    ASSERT_TRUE(value2.IsNumber());
    ASSERT_EQ(12345678901234, value2.AsInt());

    ASSERT_TRUE(IsSame(*jsEngine, value, value2));
  }
  {
    auto value = jsEngine->NewValue(true);
    ASSERT_TRUE(value.IsBool());
    ASSERT_TRUE(value.AsBool());

    AdblockPlus::JsValue value2(value);
    ASSERT_TRUE(value2.IsBool());
    ASSERT_TRUE(value2.AsBool());

    ASSERT_TRUE(IsSame(*jsEngine, value, value2));
  }
  {
    auto value = jsEngine->NewObject();
    ASSERT_TRUE(value.IsObject());
    ASSERT_EQ(0u, value.GetOwnPropertyNames().size());

    AdblockPlus::JsValue value2(value);
    ASSERT_TRUE(value2.IsObject());
    ASSERT_EQ(0u, value2.GetOwnPropertyNames().size());

    ASSERT_TRUE(IsSame(*jsEngine, value, value2));
  }
}

TEST_F(JsEngineTest, EventCallbacks)
{
  bool callbackCalled = false;
  AdblockPlus::JsValueList callbackParams;
  auto Callback = [&callbackCalled, &callbackParams](JsValueList&& params)
  {
    callbackCalled = true;
    callbackParams = move(params);
  };

  // Trigger event without a callback
  callbackCalled = false;
  jsEngine->Evaluate("_triggerEvent('foobar')");
  ASSERT_FALSE(callbackCalled);

  // Set callback
  jsEngine->SetEventCallback("foobar", Callback);
  callbackCalled = false;
  jsEngine->Evaluate("_triggerEvent('foobar', 1, 'x', true)");
  ASSERT_TRUE(callbackCalled);
  ASSERT_EQ(callbackParams.size(), 3u);
  ASSERT_EQ(callbackParams[0].AsInt(), 1);
  ASSERT_EQ(callbackParams[1].AsString(), "x");
  ASSERT_TRUE(callbackParams[2].AsBool());

  // Trigger a different event
  callbackCalled = false;
  jsEngine->Evaluate("_triggerEvent('barfoo')");
  ASSERT_FALSE(callbackCalled);

  // Remove callback
  jsEngine->RemoveEventCallback("foobar");
  callbackCalled = false;
  jsEngine->Evaluate("_triggerEvent('foobar')");
  ASSERT_FALSE(callbackCalled);
}

TEST(NewJsEngineTest, CallbackGetSet)
{
  AdblockPlus::JsEnginePtr jsEngine(AdblockPlus::JsEngine::New());

  ASSERT_TRUE(jsEngine->GetLogSystem());
  ASSERT_ANY_THROW(jsEngine->SetLogSystem(AdblockPlus::LogSystemPtr()));
  AdblockPlus::LogSystemPtr logSystem(new AdblockPlus::DefaultLogSystem());
  jsEngine->SetLogSystem(logSystem);
  ASSERT_EQ(logSystem, jsEngine->GetLogSystem());

  ASSERT_TRUE(jsEngine->GetFileSystem());
  ASSERT_ANY_THROW(jsEngine->SetFileSystem(AdblockPlus::FileSystemPtr()));
  AdblockPlus::FileSystemPtr fileSystem(new AdblockPlus::DefaultFileSystem());
  jsEngine->SetFileSystem(fileSystem);
  ASSERT_EQ(fileSystem, jsEngine->GetFileSystem());
}

TEST(NewJsEngineTest, GlobalPropertyTest)
{
  AdblockPlus::JsEnginePtr jsEngine(AdblockPlus::JsEngine::New());
  jsEngine->SetGlobalProperty("foo", jsEngine->NewValue("bar"));
  auto foo = jsEngine->Evaluate("foo");
  ASSERT_TRUE(foo.IsString());
  ASSERT_EQ(foo.AsString(), "bar");
}

TEST(NewJsEngineTest, MemoryLeak_NoCircularReferences)
{
  std::weak_ptr<AdblockPlus::JsEngine> weakJsEngine;
  {
    weakJsEngine = AdblockPlus::JsEngine::New();
  }
  EXPECT_FALSE(weakJsEngine.lock());
}

#if UINTPTR_MAX == UINT32_MAX // detection of 32-bit platform
TEST(NewJsEngineTest, 32bitsOnly_MemoryLeak_NoLeak)
#else
TEST(NewJsEngineTest, DISABLED_32bitsOnly_MemoryLeak_NoLeak)
#endif
{
  static_assert(sizeof(intptr_t) == 4, "It should be 32bit platform");
  // v8::Isolate by default requires 32MB (depends on platform), so if there is
  // a memory leak than we will run out of memory on 32 bit platform because it
  // will allocate 32000 MB which is less than 2GB where it reaches out of
  // memory. Even on android where it allocates initially 16MB, the test still
  // makes sense.
  for (int i = 0; i < 1000; ++i)
  {
    AdblockPlus::JsEngine::New();
  }
}