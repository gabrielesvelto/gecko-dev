/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim:expandtab:shiftwidth=4:tabstop=4:
 */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef widget_uikit_nsWidgetFactory_h
#define widget_uikit_nsWidgetFactory_h

#include "nscore.h"
#include "nsID.h"

class nsISupports;

nsresult nsAppShellConstructor(const nsIID& iid, void** result);

void nsWidgetUIKitModuleCtor();
void nsWidgetUIKitModuleDtor();

#endif  // defined widget_uikit_nsWidgetFactory_h
