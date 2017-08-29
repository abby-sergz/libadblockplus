/*
 * This file is part of Adblock Plus <https://adblockplus.org/>,
 * Copyright (C) 2006-present eyeo GmbH
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

#include <AdblockPlus/IFileSystem.h>
#include <stdexcept>
#include <sstream>
#include <vector>

#include <AdblockPlus/JsValue.h>
#include "FileSystemJsObject.h"
#include "JsContext.h"
#include "Utils.h"
#include "JsError.h"
#include <AdblockPlus/Platform.h>

using namespace AdblockPlus;
using AdblockPlus::Utils::ThrowExceptionInJS;

namespace
{
  void ReadCallback(const v8::FunctionCallbackInfo<v8::Value>& arguments)
  {
    AdblockPlus::JsEnginePtr jsEngine = AdblockPlus::JsEngine::FromArguments(arguments);
    AdblockPlus::JsValueList converted = jsEngine->ConvertArguments(arguments);

    v8::Isolate* isolate = arguments.GetIsolate();
    if (converted.size() != 2)
      return ThrowExceptionInJS(isolate, "_fileSystem.read requires 2 parameters");
    if (!converted[1].IsFunction())
      return ThrowExceptionInJS(isolate, "Second argument to _fileSystem.read must be a function");

    JsValueList values;
    values.push_back(converted[1]);
    auto weakCallback = jsEngine->StoreJsValues(values);
    std::weak_ptr<JsEngine> weakJsEngine = jsEngine;
    jsEngine->GetPlatform().GetFileSystem().Read(converted[0].AsString(),
      [weakJsEngine, weakCallback]
      (IFileSystem::IOBuffer&& content, const std::string& error)
      {
        auto jsEngine = weakJsEngine.lock();
        if (!jsEngine)
          return;

        const JsContext context(*jsEngine);
        auto result = jsEngine->NewObject();
        result.SetStringBufferProperty("content", std::move(content));
        if (!error.empty())
          result.SetProperty("error", error);
        jsEngine->TakeJsValues(weakCallback)[0].Call(result);
      });
  }

  void ReadFromFileCallback(const v8::FunctionCallbackInfo<v8::Value>& arguments)
  {
    AdblockPlus::JsEnginePtr jsEngine = AdblockPlus::JsEngine::FromArguments(arguments);
    AdblockPlus::JsValueList converted = jsEngine->ConvertArguments(arguments);

    v8::Isolate* isolate = arguments.GetIsolate();
    if (converted.size() != 3)
      return ThrowExceptionInJS(isolate, "_fileSystem.readFromFile requires 3 parameters");
    if (!converted[1].IsFunction())
      return ThrowExceptionInJS(isolate, "Second argument to _fileSystem.readFromFile must be a function (listener callback)");
    if (!converted[2].IsFunction())
      return ThrowExceptionInJS(isolate, "Third argument to _fileSystem.readFromFile must be a function (done callback)");

    JsValueList values;
    values.push_back(converted[1]);
    values.push_back(converted[2]);
    auto weakCallback = jsEngine->StoreJsValues(values);
    std::weak_ptr<JsEngine> weakJsEngine = jsEngine;
    jsEngine->GetPlatform().GetFileSystem().Read(converted[0].AsString(),
      [weakJsEngine, weakCallback]
    (IFileSystem::IOBuffer&& content, const std::string& error)
    {
      auto jsEngine = weakJsEngine.lock();
      if (!jsEngine)
        return;

      const JsContext context(*jsEngine);

      auto jsValues = jsEngine->TakeJsValues(weakCallback);
      if (!error.empty())
      {
        jsValues[1].Call(jsEngine->NewValue(error));
        return;
      }

      auto processFunc = jsValues[0].UnwrapValue().As<v8::Function>();

      auto globalContext = context.GetV8Context()->Global();
      if (!globalContext->IsObject())
        throw std::runtime_error("`this` pointer has to be an object");

      const v8::TryCatch tryCatch;

      const auto cend = content.cend();
      auto begin = content.begin();
      auto end = begin;
      while (begin != cend)
      {
        if (end == cend || *end == 10 || *end == 13)
        {
          if (std::distance(begin, end) != 1) // no empty lines
          {
            auto jsLine = Utils::StringBufferToV8String(jsEngine->GetIsolate(), StringBuffer(begin, end)).As<v8::Value>();
            processFunc->Call(globalContext, 1, &jsLine);
            if (tryCatch.HasCaught())
              return ThrowExceptionInJS(jsEngine->GetIsolate(), JsError::ExceptionToString(tryCatch.Exception(), tryCatch.Message()));
          }
          if (end != cend)
            ++end;
          begin = end;
        }
        if (end != cend)
          ++end;
      }
      jsValues[1].Call();
    });
  }

  void WriteCallback(const v8::FunctionCallbackInfo<v8::Value>& arguments)
  {
    AdblockPlus::JsEnginePtr jsEngine = AdblockPlus::JsEngine::FromArguments(arguments);
    AdblockPlus::JsValueList converted = jsEngine->ConvertArguments(arguments);

    v8::Isolate* isolate = arguments.GetIsolate();
    if (converted.size() != 3)
      return ThrowExceptionInJS(isolate, "_fileSystem.write requires 3 parameters");
    if (!converted[2].IsFunction())
      return ThrowExceptionInJS(isolate, "Third argument to _fileSystem.write must be a function");

    JsValueList values;
    values.push_back(converted[2]);
    auto weakCallback = jsEngine->StoreJsValues(values);
    std::weak_ptr<JsEngine> weakJsEngine = jsEngine;
    auto content = converted[1].AsStringBuffer();
    jsEngine->GetPlatform().GetFileSystem().Write(converted[0].AsString(),
      content,
      [weakJsEngine, weakCallback](const std::string& error)
      {
        auto jsEngine = weakJsEngine.lock();
        if (!jsEngine)
          return;

        const JsContext context(*jsEngine);
        JsValueList params;
        if (!error.empty())
          params.push_back(jsEngine->NewValue(error));
        jsEngine->TakeJsValues(weakCallback)[0].Call(params);
      });
  }

  void MoveCallback(const v8::FunctionCallbackInfo<v8::Value>& arguments)
  {
    AdblockPlus::JsEnginePtr jsEngine = AdblockPlus::JsEngine::FromArguments(arguments);
    AdblockPlus::JsValueList converted = jsEngine->ConvertArguments(arguments);

    v8::Isolate* isolate = arguments.GetIsolate();
    if (converted.size() != 3)
      return ThrowExceptionInJS(isolate, "_fileSystem.move requires 3 parameters");
    if (!converted[2].IsFunction())
      return ThrowExceptionInJS(isolate, "Third argument to _fileSystem.move must be a function");

    JsValueList values;
    values.push_back(converted[2]);
    auto weakCallback = jsEngine->StoreJsValues(values);
    std::weak_ptr<JsEngine> weakJsEngine = jsEngine;
    jsEngine->GetPlatform().GetFileSystem().Move(converted[0].AsString(),
      converted[1].AsString(),
      [weakJsEngine, weakCallback](const std::string& error)
      {
        auto jsEngine = weakJsEngine.lock();
        if (!jsEngine)
          return;

        const JsContext context(*jsEngine);
        JsValueList params;
        if (!error.empty())
          params.push_back(jsEngine->NewValue(error));
        jsEngine->TakeJsValues(weakCallback)[0].Call(params);
      });
  }

  void RemoveCallback(const v8::FunctionCallbackInfo<v8::Value>& arguments)
  {
    AdblockPlus::JsEnginePtr jsEngine = AdblockPlus::JsEngine::FromArguments(arguments);
    AdblockPlus::JsValueList converted = jsEngine->ConvertArguments(arguments);

    v8::Isolate* isolate = arguments.GetIsolate();
    if (converted.size() != 2)
      return ThrowExceptionInJS(isolate, "_fileSystem.remove requires 2 parameters");
    if (!converted[1].IsFunction())
      return ThrowExceptionInJS(isolate, "Second argument to _fileSystem.remove must be a function");

    JsValueList values;
    values.push_back(converted[1]);
    auto weakCallback = jsEngine->StoreJsValues(values);
    std::weak_ptr<JsEngine> weakJsEngine = jsEngine;
    jsEngine->GetPlatform().GetFileSystem().Remove(converted[0].AsString(),
      [weakJsEngine, weakCallback](const std::string& error)
      {
        auto jsEngine = weakJsEngine.lock();
        if (!jsEngine)
          return;

        const JsContext context(*jsEngine);
        JsValueList params;
        if (!error.empty())
          params.push_back(jsEngine->NewValue(error));
        jsEngine->TakeJsValues(weakCallback)[0].Call(params);
      });
  }

  void StatCallback(const v8::FunctionCallbackInfo<v8::Value>& arguments)
  {
    AdblockPlus::JsEnginePtr jsEngine = AdblockPlus::JsEngine::FromArguments(arguments);
    AdblockPlus::JsValueList converted = jsEngine->ConvertArguments(arguments);

    v8::Isolate* isolate = arguments.GetIsolate();
    if (converted.size() != 2)
      return ThrowExceptionInJS(isolate, "_fileSystem.stat requires 2 parameters");
    if (!converted[1].IsFunction())
      return ThrowExceptionInJS(isolate, "Second argument to _fileSystem.stat must be a function");

    JsValueList values;
    values.push_back(converted[1]);
    auto weakCallback = jsEngine->StoreJsValues(values);
    std::weak_ptr<JsEngine> weakJsEngine = jsEngine;
    jsEngine->GetPlatform().GetFileSystem().Stat(converted[0].AsString(),
      [weakJsEngine, weakCallback]
      (const IFileSystem::StatResult& statResult, const std::string& error)
      {
        auto jsEngine = weakJsEngine.lock();
        if (!jsEngine)
          return;

        const JsContext context(*jsEngine);
        auto result = jsEngine->NewObject();

        result.SetProperty("exists", statResult.exists);
        result.SetProperty("lastModified", statResult.lastModified);
        if (!error.empty())
          result.SetProperty("error", error);

        JsValueList params;
        params.push_back(result);
        jsEngine->TakeJsValues(weakCallback)[0].Call(params);
      });
  }
}


JsValue& FileSystemJsObject::Setup(JsEngine& jsEngine, JsValue& obj)
{
  obj.SetProperty("read", jsEngine.NewCallback(::ReadCallback));
  obj.SetProperty("readFromFile", jsEngine.NewCallback(::ReadFromFileCallback));
  obj.SetProperty("write", jsEngine.NewCallback(::WriteCallback));
  obj.SetProperty("move", jsEngine.NewCallback(::MoveCallback));
  obj.SetProperty("remove", jsEngine.NewCallback(::RemoveCallback));
  obj.SetProperty("stat", jsEngine.NewCallback(::StatCallback));
  return obj;
}
