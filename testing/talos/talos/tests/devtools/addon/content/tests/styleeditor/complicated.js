/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

"use strict";

const {
  openToolboxAndLog,
  closeToolboxAndLog,
  testSetup,
  testTeardown,
  COMPLICATED_URL,
} = require("damp-test/tests/head");

const {
  reloadStyleEditorAndLog,
} = require("damp-test/tests/styleeditor/styleeditor-helpers");

module.exports = async function () {
  await testSetup(COMPLICATED_URL);
  const toolbox = await openToolboxAndLog(
    "complicated.styleeditor",
    "styleeditor"
  );
  await reloadStyleEditorAndLog("complicated.styleeditor", toolbox);
  await closeToolboxAndLog("complicated.styleeditor", toolbox);
  await testTeardown();
};
