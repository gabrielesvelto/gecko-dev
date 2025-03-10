/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*-
 * vim: set ts=8 sts=2 et sw=2 tw=80:
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */
#include "vm/RealmFuses.h"

#include "vm/GlobalObject.h"
#include "vm/NativeObject.h"
#include "vm/ObjectOperations.h"
#include "vm/Realm.h"
#include "vm/SelfHosting.h"

using namespace js;

void js::InvalidatingRealmFuse::popFuse(JSContext* cx, RealmFuses& realmFuses) {
  InvalidatingFuse::popFuse(cx);

  for (auto& fd : realmFuses.fuseDependencies) {
    fd.invalidateForFuse(cx, this);
  }
}

bool js::InvalidatingRealmFuse::addFuseDependency(JSContext* cx,
                                                  Handle<JSScript*> script) {
  MOZ_ASSERT(script->realm() == cx->realm());
  auto* dss =
      cx->realm()->realmFuses.fuseDependencies.getOrCreateDependentScriptSet(
          cx, this);
  if (!dss) {
    return false;
  }

  return dss->addScriptForFuse(this, script);
}

void js::PopsOptimizedGetIteratorFuse::popFuse(JSContext* cx,
                                               RealmFuses& realmFuses) {
  // Pop Self.
  RealmFuse::popFuse(cx);

  // Pop associated fuse in same realm as current object.
  realmFuses.optimizeGetIteratorFuse.popFuse(cx, realmFuses);
}

void js::PopsOptimizedArrayIteratorPrototypeFuse::popFuse(
    JSContext* cx, RealmFuses& realmFuses) {
  // Pop Self.
  RealmFuse::popFuse(cx);

  // Pop associated fuse in same realm as current object.
  realmFuses.optimizeArrayIteratorPrototypeFuse.popFuse(cx, realmFuses);
}

int32_t js::RealmFuses::fuseOffsets[uint8_t(
    RealmFuses::FuseIndex::LastFuseIndex)] = {
#define FUSE(Name, LowerName) offsetof(RealmFuses, LowerName),
    FOR_EACH_REALM_FUSE(FUSE)
#undef FUSE
};

// static
int32_t js::RealmFuses::offsetOfFuseWordRelativeToRealm(
    RealmFuses::FuseIndex index) {
  int32_t base_offset = offsetof(Realm, realmFuses);
  int32_t fuse_offset = RealmFuses::fuseOffsets[uint8_t(index)];
  int32_t fuseWordOffset = GuardFuse::fuseOffset();

  return base_offset + fuse_offset + fuseWordOffset;
}

const char* js::RealmFuses::fuseNames[] = {
#define FUSE(Name, LowerName) #LowerName,
    FOR_EACH_REALM_FUSE(FUSE)
#undef FUSE
};

// TODO: It is not elegant that we have both this mechanism, but also
// GuardFuse::name, and all the overrides for naming fuses. The issue is
// that this method is static to handle consumers that don't have a
// RealmFuses around but work with indexes (e.g. spew code).
//
// I'd love it if we had a better answer.
const char* js::RealmFuses::getFuseName(RealmFuses::FuseIndex index) {
  uint8_t rawIndex = uint8_t(index);
  MOZ_ASSERT(index < RealmFuses::FuseIndex::LastFuseIndex);
  return fuseNames[rawIndex];
}

bool js::OptimizeGetIteratorFuse::checkInvariant(JSContext* cx) {
  // Simple invariant: this fuse merely reflects the conjunction of a group of
  // fuses, so if this fuse is intact, then the invariant it asserts is that
  // these two realm fuses are also intact.
  auto& realmFuses = cx->realm()->realmFuses;
  return realmFuses.arrayPrototypeIteratorFuse.intact() &&
         realmFuses.optimizeArrayIteratorPrototypeFuse.intact();
}

void js::OptimizeGetIteratorFuse::popFuse(JSContext* cx,
                                          RealmFuses& realmFuses) {
  InvalidatingRealmFuse::popFuse(cx, realmFuses);
  MOZ_ASSERT(cx->global());
  cx->runtime()->setUseCounter(cx->global(),
                               JSUseCounter::OPTIMIZE_GET_ITERATOR_FUSE);
}

bool js::OptimizeArrayIteratorPrototypeFuse::checkInvariant(JSContext* cx) {
  // Simple invariant: this fuse merely reflects the conjunction of a group of
  // fuses, so if this fuse is intact, then the invariant it asserts is that
  // these realm fuses are also intact.
  auto& realmFuses = cx->realm()->realmFuses;
  return realmFuses.arrayPrototypeIteratorNextFuse.intact() &&
         realmFuses.arrayIteratorPrototypeHasNoReturnProperty.intact() &&
         realmFuses.iteratorPrototypeHasNoReturnProperty.intact() &&
         realmFuses.arrayIteratorPrototypeHasIteratorProto.intact() &&
         realmFuses.iteratorPrototypeHasObjectProto.intact() &&
         realmFuses.objectPrototypeHasNoReturnProperty.intact();
}

static bool ObjectHasDataProperty(NativeObject* obj, PropertyKey key,
                                  Value* val) {
  mozilla::Maybe<PropertyInfo> prop = obj->lookupPure(key);
  if (prop.isNothing() || !prop->isDataProperty()) {
    return false;
  }
  *val = obj->getSlot(prop->slot());
  return true;
}

static bool ObjectHasGetterProperty(NativeObject* obj, PropertyKey key,
                                    JSFunction** getter) {
  mozilla::Maybe<PropertyInfo> prop = obj->lookupPure(key);
  if (prop.isNothing() || !prop->isAccessorProperty()) {
    return false;
  }
  JSObject* getterObject = obj->getGetter(*prop);
  if (!getterObject || !getterObject->is<JSFunction>()) {
    return false;
  }
  *getter = &getterObject->as<JSFunction>();
  return true;
}

bool js::ArrayPrototypeIteratorFuse::checkInvariant(JSContext* cx) {
  // Prototype must be Array.prototype.
  auto* proto = cx->global()->maybeGetArrayPrototype();
  if (!proto) {
    // No proto, invariant still holds
    return true;
  }

  PropertyKey iteratorKey =
      PropertyKey::Symbol(cx->wellKnownSymbols().iterator);

  // Ensure that Array.prototype's @@iterator slot is unchanged.
  Value v;
  if (!ObjectHasDataProperty(proto, iteratorKey, &v)) {
    return false;
  }
  return IsSelfHostedFunctionWithName(v, cx->names().dollar_ArrayValues_);
}

/* static */
bool js::ArrayPrototypeIteratorNextFuse::checkInvariant(JSContext* cx) {
  auto* proto = cx->global()->maybeGetArrayIteratorPrototype();

  if (!proto) {
    // Invariant holds if there is no array iterator proto.
    return true;
  }

  // Ensure that %ArrayIteratorPrototype%'s "next" slot is unchanged.
  Value v;
  if (!ObjectHasDataProperty(proto, NameToId(cx->names().next), &v)) {
    return false;
  }
  return IsSelfHostedFunctionWithName(v, cx->names().ArrayIteratorNext);
}

static bool HasNoReturnName(JSContext* cx, JS::HandleObject proto) {
  if (!proto) {
    // Invariant holds if there is no array iterator proto.
    return true;
  }

  JS::RootedId returnName(cx, NameToId(cx->names().return_));

  // An alternative design here would chain together all the has-return-property
  // fuses such that the fuses each express a stronger invariant; for now these
  // fuses have only the invariant that each object -itself- has no return
  // property.
  bool found = true;
  if (!HasOwnProperty(cx, proto, returnName, &found)) {
    cx->recoverFromOutOfMemory();
    return true;
  }

  return !found;
}

/* static */
bool js::ArrayIteratorPrototypeHasNoReturnProperty::checkInvariant(
    JSContext* cx) {
  RootedObject proto(cx, cx->global()->maybeGetArrayIteratorPrototype());

  if (!proto) {
    // Invariant holds if there is no array iterator proto.
    return true;
  }

  return HasNoReturnName(cx, proto);
}

/* static */
bool js::IteratorPrototypeHasNoReturnProperty::checkInvariant(JSContext* cx) {
  RootedObject proto(cx, cx->global()->maybeGetIteratorPrototype());

  if (!proto) {
    // Invariant holds if there is no array iterator proto.
    return true;
  }

  return HasNoReturnName(cx, proto);
}

/* static */
bool js::ArrayIteratorPrototypeHasIteratorProto::checkInvariant(JSContext* cx) {
  RootedObject proto(cx, cx->global()->maybeGetArrayIteratorPrototype());
  if (!proto) {
    // Invariant holds if there is no array iterator proto.
    return true;
  }

  RootedObject iterProto(cx, cx->global()->maybeGetIteratorPrototype());
  if (!iterProto) {
    MOZ_CRASH("Can we have the array iter proto without the iterator proto?");
    return true;
  }

  return proto->staticPrototype() == iterProto;
}

/* static */
bool js::IteratorPrototypeHasObjectProto::checkInvariant(JSContext* cx) {
  RootedObject proto(cx, cx->global()->maybeGetIteratorPrototype());
  if (!proto) {
    // Invariant holds if there is no array iterator proto.
    return true;
  }

  return proto->staticPrototype() == &cx->global()->getObjectPrototype();
}

/* static */
bool js::ObjectPrototypeHasNoReturnProperty::checkInvariant(JSContext* cx) {
  RootedObject proto(cx, &cx->global()->getObjectPrototype());
  return HasNoReturnName(cx, proto);
}

void js::OptimizeArraySpeciesFuse::popFuse(JSContext* cx,
                                           RealmFuses& realmFuses) {
  InvalidatingRealmFuse::popFuse(cx, realmFuses);
  MOZ_ASSERT(cx->global());
  cx->runtime()->setUseCounter(cx->global(),
                               JSUseCounter::OPTIMIZE_ARRAY_SPECIES_FUSE);
}

bool js::OptimizeArraySpeciesFuse::checkInvariant(JSContext* cx) {
  // Prototype must be Array.prototype.
  auto* proto = cx->global()->maybeGetArrayPrototype();
  if (!proto) {
    // No proto, invariant still holds
    return true;
  }

  auto* ctor = cx->global()->maybeGetConstructor(JSProto_Array);
  MOZ_ASSERT(ctor);

  // Ensure Array.prototype's `constructor` slot is the `Array` constructor.
  Value v;
  if (!ObjectHasDataProperty(proto, NameToId(cx->names().constructor), &v)) {
    return false;
  }
  if (v != ObjectValue(*ctor)) {
    return false;
  }

  // Ensure Array's `@@species` slot is the $ArraySpecies getter.
  PropertyKey speciesKey = PropertyKey::Symbol(cx->wellKnownSymbols().species);
  JSFunction* getter = nullptr;
  if (!ObjectHasGetterProperty(&ctor->as<NativeObject>(), speciesKey,
                               &getter)) {
    return false;
  }
  return IsSelfHostedFunctionWithName(getter, cx->names().dollar_ArraySpecies_);
}
