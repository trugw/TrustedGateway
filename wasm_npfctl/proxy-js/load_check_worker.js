
/*
var filename;
if (!filename) {
  filename = 'http://localhost:8000/webapp-npfctl/npfctl.worker.js';
}

var workerURL = filename;
if (SUPPORT_BASE64_EMBEDDING) {
  var fileBytes = tryParseAsDataURI(filename);
  if (fileBytes) {
    workerURL = URL.createObjectURL(new Blob([fileBytes], {type: 'application/javascript'}));
  }
}
*/

//var filename = 'http://localhost:8000/webapp-npfctl/npfctl.worker.js';
var filename = location.protocol + '//' + location.hostname + ':8000/webapp-npfctl/npfctl.worker.js';
var resp = $.ajax ({
  type: "GET",
  async: false,
  url: filename,
});

// SHA256(./npfctl.worker.js)= 0233512520b3e38c8fb6a8776600ac0452ef3ac1b6b21b1ca51b8120dba311bb
var refWorkerHash = '12498b88070c2b89aea7ed384d2e895e5d52763689fe6bf8687dc3877829ca40';
var worker_hash = sjcl.codec.hex.fromBits(sjcl.hash.sha256.hash(resp.responseText));
console.log('worker sha256: ' + worker_hash);
if (refWorkerHash === worker_hash) {
  console.log('Hash matches reference value!');
} else {
  console.log('SECURITY FAILURE: worker hash does not match expected value.');
  throw 'SECURITY FAILURE: worker hash does not match expected value.';
}

var workerJS = 'var base_addr = "' + location.protocol + '//' + location.hostname + '";\n\n';
workerJS += resp.responseText;

//var blob = new Blob([resp.responseText], {type: 'application/javascript'});
var blob = new Blob([workerJS], {type: 'application/javascript'});

//filename = URL.createObjectURL(blob);
var worker = new Worker(URL.createObjectURL(blob));

