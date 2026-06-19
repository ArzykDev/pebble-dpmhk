// Trip-route orchestration: packet lookup -> /trasa (both directions) ->
// pick the travelled direction -> downstream stop slice from the current stop.
// The /trasa endpoint returns stop names + a global `order` index, no times.

var api = require('./api');
var dates = require('./date');
var appmsg = require('./appmsg');
var util = require('./util');

var MAX_ROWS = 18; // keep in sync with MAX_TRIP in model.h

// Fetch both directions concurrently (they're independent). Either may fail on
// its own; we degrade to whichever we got. cb(err, dir0, dir1) where a failed
// direction is null.
function getBoth(packet, line, cb) {
  var pending = 2;
  var dir0, dir1, err0, err1;
  function done() {
    if (--pending > 0) {
      return;
    }
    if ((err0 || !dir0) && (err1 || !dir1)) {
      cb(err0 || err1);
      return;
    }
    cb(null, err0 ? null : dir0, err1 ? null : dir1);
  }
  api.getRoute(packet, line, '0', function (e, d) {
    err0 = e;
    dir0 = d;
    done();
  });
  api.getRoute(packet, line, '1', function (e, d) {
    err1 = e;
    dir1 = d;
    done();
  });
}

// Build a folded-name -> order map across both directions (union).
function buildOrderMap(dir0, dir1) {
  var map = {};
  [dir0, dir1].forEach(function (dir) {
    if (!dir) return;
    dir.forEach(function (s) {
      var key = util.foldName(s.name);
      // Skip non-numeric orders: a NaN would silently bias the direction pick
      // in resolveDownstream toward dir0. Absent keys fall back gracefully.
      if (!(key in map) && typeof s.order === 'number' && isFinite(s.order)) {
        map[key] = s.order;
      }
    });
  });
  return map;
}

// Index of the folded stop name in a direction array, or -1.
function indexOfStop(dir, foldedName) {
  for (var i = 0; i < dir.length; i++) {
    if (util.foldName(dir[i].name) === foldedName) {
      return i;
    }
  }
  return -1;
}

// Downstream stop names from the current stop toward the terminus.
// Returns null when the current stop can't be located in either direction.
function resolveDownstream(dir0, dir1, currentStopName, destText) {
  var map = buildOrderMap(dir0, dir1);
  var curFold = util.foldName(currentStopName);
  var dstFold = util.foldName(destText);
  var cur = map[curFold];
  var dst = map[dstFold];

  // smer=0 travels in ascending `order`, smer=1 descending. The destination is
  // "ahead": if its order is below the current stop's, the bus goes descending.
  var primary = (dst !== undefined && cur !== undefined && dst < cur)
    ? dir1
    : dir0;
  var secondary = (primary === dir1) ? dir0 : dir1;

  // The current stop may be missing from one direction (one-way segments);
  // fall back to the other direction's array.
  var dir = primary;
  var curIdx = dir ? indexOfStop(dir, curFold) : -1;
  if (curIdx < 0) {
    dir = secondary;
    curIdx = dir ? indexOfStop(dir, curFold) : -1;
  }
  if (curIdx < 0) {
    return null; // current stop not on this line in either direction
  }

  // Cut the slice at the destination if we can find it downstream; otherwise
  // keep everything to the end (branch/terminus named differently than smer).
  var endIdx = dir.length - 1;
  var dstIdx = indexOfStop(dir, dstFold);
  if (dstIdx > curIdx) {
    endIdx = dstIdx;
  }

  var names = [];
  for (var i = curIdx + 1; i <= endIdx; i++) {
    names.push(dir[i].name);
  }
  return names;
}

// cb(errCode, names[])
function fetch(line, destText, currentStopName, cb) {
  api.getPackets(function (err, packets) {
    if (err) {
      cb(appmsg.errCode(err));
      return;
    }
    var packet = dates.pickPacket(packets, new Date());
    if (!packet) {
      cb(appmsg.ERR.NO_PACKET);
      return;
    }
    getBoth(packet, line, function (err2, dir0, dir1) {
      if (err2) {
        cb(appmsg.errCode(err2));
        return;
      }
      var names = resolveDownstream(dir0, dir1, currentStopName, destText);
      if (names === null) {
        cb(appmsg.ERR.PARSE);
        return;
      }
      console.log('trip ' + line + ' -> ' + destText + ' from ' +
                  currentStopName + ': ' + names.length + ' stops');
      cb(appmsg.ERR.NONE, names.slice(0, MAX_ROWS));
    });
  });
}

module.exports = {
  fetch: fetch,
};
