function postCustomMessage(data) {
  postMessage({ target: 'custom', userData: data });
}

// --pre-jses are emitted after the Module integration code, so that they can
// refer to Module (if they choose; they can also define Module)
/* Define Custom Post Message handler for the client-side code */
Module['onCustomMessage'] = function(message) {
  //console.log(message);
  switch(message.data.userData.type) {
    case 'setURL': {
      console.log('setURL command');
      Module.ccall('set_bstgw_base_url', null, ['string'], [message.data.userData.url]);
      //Module.cwrap('set_bstgw_base_url', null, ['string'])(message.data.userData.url);
      break;
    }
    case 'submitConfig': {
      console.log('writing config file');
      try {
        FS.writeFile('/etc/npf.conf', message.data.userData.conf, { flags: 'w+' });
        callNpfctl(['reload']);
      } catch(e) {
        console.log('writing to npf.conf failed');
      }
      break;
    }
    case 'command': {
      var ret = callNpfctl(message.data.userData.cmd);
      postCustomMessage({'type': 'RESULT', 'ret:': ret});
      break;
    }
    case 'fileStats': {
      try {
        console.log( FS.stat(message.data.userData.path) );
      } catch (e) {
        console.log( message.data.userData.path, 'does not exist' );
      }
      break;
    }
    default: {
      console.log("Test from worker"); // crash bcs. not main?
      postMessage({'target': 'stdout', 'content': 'hello'});
      callNpfctl();
      postCustomMessage('hello');
    }
  }
};
//Module['noInitialRun'] = true; // seems to make main thread hang for a while, s.t. custom message not trigger behaviour?!
Module['onRuntimeInitialized'] = function() {
  console.log("Client Runtime intialized"); // this one works randomly

  // create directories for npfctl
  try {
    FS.mkdir('/etc');
    FS.mkdir('/var');
    FS.mkdir('/var/db');
  } catch(e) {
    console.log('Failed setting up npfctl directories: ' + e.message);
  }

  postCustomMessage({'type': 'INIT'});
};

function callNpfctl(args) {
  var npfctl = Module.cwrap('npfctl_main', 'number', ['number', 'number']);
  var prgname = "./npfctl";

  /* from callMain() */
  args = args || [];

  var marker = stackSave();
  //console.log("before: "+marker);

  var argc = args.length+1;
  var argv = stackAlloc((argc + 1) * 4);
  HEAP32[argv >> 2] = allocateUTF8OnStack(prgname);
  for (var i = 1; i < argc; i++) {
    HEAP32[(argv >> 2) + i] = allocateUTF8OnStack(args[i - 1]);
  }
  HEAP32[(argv >> 2) + argc] = 0;

  try {
    npfctl(argc, argv);
  } catch(e) {}

  //console.log("after: "+stackSave());
  stackRestore(marker);
};


var base_addr;
var wasm_addr;
if (!base_addr) {
  wasm_addr = 'http://localhost:8000/webapp/npfctl.wasm';
} else {
  wasm_addr = base_addr + ':8000/webapp/npfctl.wasm';
}
console.log('Will fetch wasm from: ' + wasm_addr);

var wasm_xhr = new XMLHttpRequest();
wasm_xhr.open('GET', wasm_addr, false);
wasm_xhr.responseType = 'arraybuffer';
wasm_xhr.send(null);

// SHA256(./npfctl.wasm)= a3c9bac2595298e4ff741bab771450a032abf2ca4a018d88f54f281cd71e622b
var refWasmHash ='a3c9bac2595298e4ff741bab771450a032abf2ca4a018d88f54f281cd71e622b';
var wasm_hash = sjcl.codec.hex.fromBits(sjcl.hash.sha256.hash(
  sjcl.codec.bytes.toBits(new Uint8Array(wasm_xhr.response)))
);

console.log('wasm sha256: ' + wasm_hash);
if (refWasmHash === wasm_hash) {
  console.log('Hash matches reference value!');
} else {
  console.log('SECURITY FAILURE: wasm hash does not match expected value.');
  throw 'SECURITY FAILURE: wasm hash does not match expected value.';
}

Module['wasmBinary'] = wasm_xhr.response;
