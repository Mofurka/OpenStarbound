#include "StarLuaAssetBindings.hpp"
#include "StarImage.hpp"
#include "StarRoot.hpp"
#include "StarLogging.hpp"
#include "StarFormat.hpp"
#include "StarBuffer.hpp"
#include "StarImageLuaBindings.hpp"
#include "StarAssets.hpp"
#include "StarMemoryAssetSource.hpp"

namespace Star {

namespace {

MemoryAssetSourcePtr getTemporaryAssetSource() {
  static MemoryAssetSourcePtr tempSource;
  static bool initialized = false;

  if (!initialized) {
    JsonObject metadata;
    metadata["name"] = "Temporary Assets";
    metadata["description"] = "Runtime-created assets from Lua";
    tempSource = make_shared<MemoryAssetSource>("temp", metadata);

    // Register in the Assets system
    auto& root = Root::singleton();
    const auto assets = const_cast<Assets*>(root.assets().get());
    assets->addRuntimeSource("/temp", tempSource);

    initialized = true;
    Logger::info("Initialized temporary asset source");
  }

  return tempSource;
}

}

LuaCallbacks LuaBindings::makeAssetCallbacks() {
  LuaCallbacks callbacks;

  callbacks.registerCallback("makeAsset", [](LuaEngine& engine, String const& path, LuaValue const& data) -> String {
    if (!path.beginsWith("/temp/")) {
      throw StarException(strf("Temporary asset path must start with '/temp/', got: {}", path));
    }

    const auto tempSource = getTemporaryAssetSource();

    if (auto str = engine.luaMaybeTo<String>(data)) {
      ByteArray bytes(str->utf8Ptr(), str->utf8Size());
      const size_t byteSize = bytes.size();  // Save size before move
      tempSource->set(path, std::move(bytes));
      Logger::info("Created temporary asset (bytes): {} ({} bytes)", path, byteSize);
    } else if (auto image = engine.luaMaybeTo<Image>(data)) {
      Logger::info("Created temporary asset (image): {} ({}x{})", path, image->width(), image->height());
      tempSource->set(path, *image);
    } else {
      const auto json = engine.luaTo<Json>(data).repr();
      ByteArray bytes(json.utf8Ptr(), json.utf8Size());
      const size_t byteSize = bytes.size();  // Save size before move
      tempSource->set(path, std::move(bytes));
      Logger::info("Created temporary asset (JSON): {} ({} bytes)", path, byteSize);
    }

    auto& root = Root::singleton();
    const auto assets = const_cast<Assets*>(root.assets().get());
    assets->addRuntimeSource("/temp", tempSource);

    return path;
  });

  callbacks.registerCallback("makeAssetFromBytes", [](String const& path, String const& bytes) -> String {
    if (!path.beginsWith("/temp/")) {
      throw StarException(strf("Temporary asset path must start with '/temp/', got: {}", path));
    }

    const auto tempSource = getTemporaryAssetSource();
    ByteArray byteArray(bytes.utf8Ptr(), bytes.utf8Size());
    tempSource->set(path, std::move(byteArray));

    Logger::info("Created temporary asset: {} ({} bytes)", path, bytes.size());

    auto& root = Root::singleton();
    const auto assets = const_cast<Assets*>(root.assets().get());
    assets->addRuntimeSource("/temp", tempSource);

    return path;
  });

  callbacks.registerCallback("makeImageFromBytes", [](String const& path, String const& bytes) -> String {
    if (!path.beginsWith("/temp/")) {
      throw StarException(strf("Temporary image path must start with '/temp/', got: {}", path));
    }

    try {
      ByteArray byteArray(bytes.utf8Ptr(), bytes.utf8Size());

      const auto buffer = make_shared<Buffer>(byteArray);

      Image image = Image::readPng(buffer);

      const unsigned width = image.width();
      const unsigned height = image.height();

      // Verify image was loaded
      if (width == 0 || height == 0) {
        throw StarException(strf("Failed to load image: resulted in 0x0 size. Data size: {} bytes", byteArray.size()));
      }

      const auto tempSource = getTemporaryAssetSource();
      tempSource->set(path, std::move(image));

      Logger::info("Created temporary image: {} ({}x{})", path, width, height);

      auto& root = Root::singleton();
      const auto assets = const_cast<Assets*>(root.assets().get());
      assets->addRuntimeSource("/temp", tempSource);

      return path;
    } catch (StarException const&) {
      throw;
    } catch (std::exception const& e) {
      throw StarException(strf("Failed to create image from bytes: {}", e.what()));
    }
  });

  callbacks.registerCallback("getTemporaryAsset", [](String const& path) -> Maybe<String> {
    if (const auto tempSource = getTemporaryAssetSource(); tempSource->contains(path)) {
      auto bytes = tempSource->read(path);
      return String(bytes.ptr(), bytes.size());
    }
    return {};
  });

  callbacks.registerCallback("getTemporaryImage", [](String const& path) -> Maybe<Image> {
    const auto tempSource = getTemporaryAssetSource();

    if (const auto image = tempSource->image(path)) {
      return *image;
    }
    return {};
  });

  callbacks.registerCallback("hasTemporaryAsset", [](String const& path) -> bool {
    const auto tempSource = getTemporaryAssetSource();
    return tempSource->contains(path);
  });

  callbacks.registerCallback("removeTemporaryAsset", [](String const& path) {
    if (const auto tempSource = getTemporaryAssetSource(); tempSource->erase(path)) {
      Logger::info("Removed temporary asset: {}", path);
    }
  });

  //TODO: find reason why temp assets are not being cleared
  callbacks.registerCallback("clearTemporaryAssets", []() {
    JsonObject metadata;
    metadata["name"] = "Temporary Assets";
    metadata["description"] = "Runtime-created assets from Lua";
    const auto newSource = make_shared<MemoryAssetSource>("temp", metadata);

    getTemporaryAssetSource() = newSource;

    Logger::info("Cleared all temporary assets");
  });

  return callbacks;
}

}

