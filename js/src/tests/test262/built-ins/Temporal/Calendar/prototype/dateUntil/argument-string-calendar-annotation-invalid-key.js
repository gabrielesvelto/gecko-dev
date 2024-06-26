// |reftest| skip-if(!this.hasOwnProperty('Temporal')) -- Temporal is not enabled unconditionally
// Copyright (C) 2024 Igalia, S.L. All rights reserved.
// This code is governed by the BSD license found in the LICENSE file.

/*---
esid: sec-temporal.calendar.prototype.dateuntil
description: Annotation keys are lowercase-only
features: [Temporal]
---*/

const invalidStrings = [
  ["1970-01-01[U-CA=iso8601]", "invalid capitalized key"],
  ["1970-01-01[u-CA=iso8601]", "invalid partially-capitalized key"],
  ["1970-01-01[FOO=bar]", "invalid capitalized unrecognized key"],
];
const instance = new Temporal.Calendar("iso8601");
invalidStrings.forEach(([arg, descr]) => {
  assert.throws(
    RangeError,
    () => instance.dateUntil(arg, new Temporal.PlainDate(1977, 11, 19)),
    `annotation keys must be lowercase: ${arg} - ${descr} (first argument)`
  );
  assert.throws(
    RangeError,
    () => instance.dateUntil(new Temporal.PlainDate(1977, 11, 19), arg),
    `annotation keys must be lowercase: ${arg} - ${descr} (second argument)`
  );
});

reportCompare(0, 0);
