/* -*- Mode: IDL; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/.
 *
 * The origin of this IDL file is
 * https://xhr.spec.whatwg.org/#interface-xmlhttprequest
 *
 * Copyright © 2012 W3C® (MIT, ERCIM, Keio), All Rights Reserved. W3C
 * liability, trademark and document use rules apply.
 */

interface InputStream;
interface MozChannel;

enum XMLHttpRequestResponseType {
  "",
  "arraybuffer",
  "blob",
  "document",
  "json",
  "text",
};

/**
 * Parameters for instantiating an XMLHttpRequest. They are passed as an
 * optional argument to the constructor:
 *
 *  new XMLHttpRequest({anon: true, system: true});
 */
dictionary MozXMLHttpRequestParameters
{
  /**
   * If true, the request will be sent without cookie and authentication
   * headers. Defaults to true for system/privileged/chrome requests,
   * and to false otherwise.
   * Note that even if set to true, for system/privileged/chrome requests,
   * manually-set 'Cookie' headers are not removed.
   */
  boolean mozAnon;

  /**
   * If true, the same origin policy will not be enforced on the request.
   */
  boolean mozSystem = false;
};

[Exposed=(Window,DedicatedWorker,SharedWorker)]
interface XMLHttpRequest : XMLHttpRequestEventTarget {
  [Throws]
  constructor(optional MozXMLHttpRequestParameters params = {});
  // There are apparently callers, specifically CoffeeScript, who do
  // things like this:
  //   c = new(window.ActiveXObject || XMLHttpRequest)("Microsoft.XMLHTTP")
  // To handle that, we need a constructor that takes a string.
  [Throws]
  constructor(DOMString ignored);

  // event handler
  attribute EventHandler onreadystatechange;

  // states
  const unsigned short UNSENT = 0;
  const unsigned short OPENED = 1;
  const unsigned short HEADERS_RECEIVED = 2;
  const unsigned short LOADING = 3;
  const unsigned short DONE = 4;

  readonly attribute unsigned short readyState;

  // request
  [Throws]
  undefined open(ByteString method, USVString url);
  [Throws]
  undefined open(ByteString method, USVString url, boolean async,
            optional USVString? user=null, optional USVString? password=null);
  [Throws]
  undefined setRequestHeader(ByteString header, ByteString value);

  [SetterThrows]
  attribute unsigned long timeout;

  [SetterThrows]
  attribute boolean withCredentials;

  [Throws]
  readonly attribute XMLHttpRequestUpload upload;

  [Throws]
  undefined send(optional (Document or XMLHttpRequestBodyInit)? body = null);

  [Throws]
  undefined abort();

  // response
  readonly attribute USVString responseURL;

  [Throws]
  readonly attribute unsigned short status;

  [Throws]
  readonly attribute ByteString statusText;

  [Throws]
  ByteString? getResponseHeader(ByteString header);

  [Throws]
  ByteString getAllResponseHeaders();

  [Throws]
  undefined overrideMimeType(DOMString mime);

  [SetterThrows]
  attribute XMLHttpRequestResponseType responseType;
  [Throws]
  readonly attribute any response;
  [Cached, Pure, Throws]
  readonly attribute USVString? responseText;

  [Throws, Exposed=Window]
  readonly attribute Document? responseXML;

  // Mozilla-specific stuff

  [ChromeOnly, SetterThrows]
  attribute boolean mozBackgroundRequest;

  [ChromeOnly, Exposed=Window]
  readonly attribute MozChannel? channel;

  [Throws, ChromeOnly, Exposed=Window]
  any getInterface(any iid);

  [ChromeOnly, Exposed=Window]
  undefined setOriginAttributes(optional OriginAttributesDictionary originAttributes = {});

  [ChromeOnly, Throws]
  undefined sendInputStream(InputStream body);

  // Only works on MainThread.
  // Its permanence is to be evaluated in bug 1368540 for Firefox 60.
  [ChromeOnly]
  readonly attribute unsigned short errorCode;

  readonly attribute boolean mozAnon;
  readonly attribute boolean mozSystem;
};
