// AppMessage protocol: header message + one chained message per row.
// Shared contract with src/c/comm.c — keep OP/ERR/flag values in sync.

var OP = {
  GET_DEPARTURES: 1,
  GET_NEAREST: 2,
  GET_TRIP: 3,
  FAVORITES: 4,
  ADD_FAVORITE: 5,
  REMOVE_FAVORITE: 6,
};

var ERR = {
  NONE: 0,
  NETWORK: 1,
  API: 2,
  PARSE: 3,
  NO_PACKET: 4,
  GPS: 5,
};

var DELAY_UNKNOWN = -32768;

// Map an api.js error object to a protocol ERR code
function errCode(err) {
  if (!err) return ERR.NONE;
  if (err.type === 'api') return ERR.API;
  if (err.type === 'parse') return ERR.PARSE;
  if (err.type === 'no_packet') return ERR.NO_PACKET;
  return ERR.NETWORK;
}

var SEND_ATTEMPTS = 3;
var SEND_RETRY_MS = 150;

// Sends messages sequentially, chaining on ACK. NACKs (usually a transient
// APP_MSG_BUSY) are retried — a silently broken chain would leave the watch
// with a partial board or a stuck loading state.
function sendChain(messages, done) {
  if (messages.length === 0) {
    if (done) done(null);
    return;
  }
  var msg = messages[0];
  var attempt = 0;

  function trySend() {
    Pebble.sendAppMessage(
      msg,
      function () {
        messages.shift();
        sendChain(messages, done);
      },
      function (e) {
        attempt++;
        if (attempt < SEND_ATTEMPTS) {
          setTimeout(trySend, SEND_RETRY_MS * attempt);
        } else {
          console.log('appmsg send failed after ' + attempt + ' attempts: ' +
                      JSON.stringify(e && e.error));
          if (done) done(e || new Error('send failed'));
        }
      }
    );
  }
  trySend();
}

// items: [{line, dest, time, delay}]
// stopName may be null — the watch then keeps the name it already has.
function sendDepartures(requestId, stopName, items, opts) {
  opts = opts || {};
  var header = {
    REQUEST_ID: requestId,
    OP: OP.GET_DEPARTURES,
    META_COUNT: items.length,
    META_FLAGS: opts.flags || 0,
    META_FETCHED_AT: opts.fetchedAt || '',
    ERROR: opts.error || ERR.NONE,
  };
  if (stopName) {
    header.META_STOP_NAME = stopName;
  }
  var msgs = [header];
  items.forEach(function (it, i) {
    msgs.push({
      REQUEST_ID: requestId,
      OP: OP.GET_DEPARTURES,
      ROW_INDEX: i,
      ROW_LINE: it.line,
      ROW_DEST: it.dest,
      ROW_TIME: it.time,
      ROW_DELAY: it.delay === null || it.delay === undefined
        ? DELAY_UNKNOWN
        : it.delay,
    });
  });
  sendChain(msgs);
}

function sendDeparturesError(requestId, error) {
  sendDepartures(requestId, null, [], { error: error });
}

// stops: [{id, name, dist}] — dist is a preformatted string like "320 m"
function sendStops(requestId, op, stops, error) {
  var msgs = [
    {
      REQUEST_ID: requestId,
      OP: op,
      META_COUNT: stops.length,
      ERROR: error || ERR.NONE,
    },
  ];
  stops.forEach(function (s, i) {
    msgs.push({
      REQUEST_ID: requestId,
      OP: op,
      ROW_INDEX: i,
      META_COUNT: stops.length,
      ROW_LINE: s.name,
      ROW_META: String(s.id),
      ROW_TIME: s.dist || '',
    });
  });
  sendChain(msgs);
}

// names: [stopName, ...] in travel order. Reuses the stops-row convention
// ROW_LINE = name (no id/time/delay for a route).
function sendTrip(requestId, names) {
  var msgs = [
    {
      REQUEST_ID: requestId,
      OP: OP.GET_TRIP,
      META_COUNT: names.length,
      ERROR: ERR.NONE,
    },
  ];
  names.forEach(function (name, i) {
    msgs.push({
      REQUEST_ID: requestId,
      OP: OP.GET_TRIP,
      ROW_INDEX: i,
      META_COUNT: names.length,
      ROW_LINE: name,
    });
  });
  sendChain(msgs);
}

function sendTripError(requestId, error) {
  sendChain([
    {
      REQUEST_ID: requestId,
      OP: OP.GET_TRIP,
      META_COUNT: 0,
      ERROR: error,
    },
  ]);
}

module.exports = {
  OP: OP,
  ERR: ERR,
  errCode: errCode,
  sendDepartures: sendDepartures,
  sendDeparturesError: sendDeparturesError,
  sendStops: sendStops,
  sendTrip: sendTrip,
  sendTripError: sendTripError,
};
