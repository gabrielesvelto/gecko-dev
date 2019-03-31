/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef GFX_LAYERRENDERSTATE_H
#define GFX_LAYERRENDERSTATE_H

#include "Units.h"
#include "mozilla/gfx/Point.h"   // for IntPoint
#include "mozilla/Maybe.h"

#ifdef MOZ_WIDGET_GONK
#  include <utils/RefBase.h>
#if ANDROID_VERSION >= 21
#  include <utils/NativeHandle.h>
#endif
#endif

namespace android {
class MOZ_EXPORT GraphicBuffer;
}  // namespace android

namespace mozilla {
namespace layers {

class TextureHost;

#define INVALID_OVERLAY -1

// LayerRenderState for Composer2D
// We currently only support Composer2D using gralloc. If we want to be backed
// by other surfaces we will need a more generic LayerRenderState.
enum class LayerRenderStateFlags : int8_t {
  LAYER_RENDER_STATE_DEFAULT = 0,
  ORIGIN_BOTTOM_LEFT = 1 << 0,
  BUFFER_ROTATION = 1 << 1,
  // Notify Composer2D to swap the RB pixels of gralloc buffer
  FORMAT_RB_SWAP = 1 << 2,
  // We record opaqueness here alongside the actual surface we're going to
  // render. This avoids confusion when a layer might return different kinds
  // of surfaces over time (e.g. video frames).
  OPAQUE = 1 << 3
};
MOZ_MAKE_ENUM_CLASS_BITWISE_OPERATORS(LayerRenderStateFlags)

// The 'ifdef MOZ_WIDGET_GONK' sadness here is because we don't want to include
// android::sp unless we have to.
struct LayerRenderState {
  // Constructors and destructor are defined in LayersTypes.cpp so we don't
  // have to pull in a definition for GraphicBuffer.h here. In KK at least,
  // that results in nasty pollution such as libui's hardware.h #defining
  // 'version_major' and 'version_minor' which conflict with Theora's codec.c...
  LayerRenderState();
  LayerRenderState(const LayerRenderState& aOther);
  ~LayerRenderState();

#ifdef MOZ_WIDGET_GONK
  LayerRenderState(android::GraphicBuffer* aSurface,
                   const gfx::IntSize& aSize,
                   LayerRenderStateFlags aFlags,
                   TextureHost* aTexture);

  bool OriginBottomLeft() const
  { return bool(mFlags & LayerRenderStateFlags::ORIGIN_BOTTOM_LEFT); }

  bool BufferRotated() const
  { return bool(mFlags & LayerRenderStateFlags::BUFFER_ROTATION); }

  bool FormatRBSwapped() const
  { return bool(mFlags & LayerRenderStateFlags::FORMAT_RB_SWAP); }

  void SetOverlayId(const int32_t& aId)
  { mOverlayId = aId; }

  android::GraphicBuffer* GetGrallocBuffer() const
  { return mSurface.get(); }

#if ANDROID_VERSION >= 21
  android::NativeHandle* GetSidebandStream() const
  { return mSidebandStream.get(); }
#endif
#endif

  void SetOffset(const nsIntPoint& aOffset)
  {
    mOffset = aOffset;
    mHasOwnOffset = true;
  }

  // see LayerRenderStateFlags
  LayerRenderStateFlags mFlags;
  // true if mOffset is applicable
  bool mHasOwnOffset;
  // the location of the layer's origin on mSurface
  nsIntPoint mOffset;
  // The 'ifdef MOZ_WIDGET_GONK' sadness here is because we don't want to include
  // android::sp unless we have to.
#ifdef MOZ_WIDGET_GONK
  // surface to render
  android::sp<android::GraphicBuffer> mSurface;
  int32_t mOverlayId;
  // size of mSurface
  gfx::IntSize mSize;
  TextureHost* mTexture;
#if ANDROID_VERSION >= 21
  android::sp<android::NativeHandle> mSidebandStream;
#endif
#endif
};

}  // namespace layers
}  // namespace mozilla

#endif /* GFX_LAYERRENDERSTATE_H */
