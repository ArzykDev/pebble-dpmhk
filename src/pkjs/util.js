// Shared small helpers.

// localeCompare needs ICU which pypkjs (and some phone runtimes) lack —
// fold Czech diacritics to ASCII for sorting/matching instead.
var FOLD = {
  'á': 'a', 'č': 'c', 'ď': 'd', 'é': 'e', 'ě': 'e', 'í': 'i', 'ň': 'n',
  'ó': 'o', 'ř': 'r', 'š': 's', 'ť': 't', 'ú': 'u', 'ů': 'u', 'ý': 'y',
  'ž': 'z',
};

function foldName(name) {
  var out = '';
  var lower = String(name).toLowerCase();
  for (var i = 0; i < lower.length; i++) {
    var ch = lower.charAt(i);
    out += FOLD[ch] || ch;
  }
  return out;
}

function compareNames(a, b) {
  var ka = foldName(a);
  var kb = foldName(b);
  return ka < kb ? -1 : ka > kb ? 1 : 0;
}

module.exports = {
  foldName: foldName,
  compareNames: compareNames,
};
