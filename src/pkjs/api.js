// api.dpmhk.cz client — the ONLY file that knows the DPMHK API contracts.
// Undocumented official backend (dpmhk.cz "Spoje-vyhledavac" plugin).
// Gotchas: datum is DD_MM_YYYY with underscores; linka is space-padded.

var BASE_URL = 'https://api.dpmhk.cz';
var TIMEOUT_MS = 8000;

// Error types map to the ERR codes in appmsg.js / model.h
function request(method, path, body, cb) {
  var xhr = new XMLHttpRequest();
  xhr.open(method, BASE_URL + path);
  xhr.timeout = TIMEOUT_MS;
  if (body) {
    xhr.setRequestHeader('Content-Type', 'application/json');
  }
  xhr.onload = function () {
    if (xhr.status !== 200) {
      cb({ type: 'api', status: xhr.status });
      return;
    }
    var parsed;
    try {
      parsed = JSON.parse(xhr.responseText);
    } catch (err) {
      cb({ type: 'parse' });
      return;
    }
    cb(null, parsed);
  };
  xhr.ontimeout = function () {
    cb({ type: 'network' });
  };
  xhr.onerror = function () {
    cb({ type: 'network' });
  };
  xhr.send(body ? JSON.stringify(body) : null);
}

// The weekly packet list barely changes, but a single departures tap with a
// stop name resolves the packet twice (cache.getStations + departures.fetch).
// A short in-memory TTL coalesces those into one /packet GET without affecting
// packet-rollover detection (weekly, so a ~minute of lag is harmless).
var PACKETS_TTL_MS = 60000;
var s_packets = null; // { at: epochMs, list: [...] }

// cb(err, [{packet:"185", from:"2026-06-01", to:"2026-06-08"}, ...])
function getPackets(cb) {
  if (s_packets && Date.now() - s_packets.at < PACKETS_TTL_MS) {
    cb(null, s_packets.list);
    return;
  }
  request('GET', '/packet', null, function (err, list) {
    if (err) {
      cb(err);
      return;
    }
    if (list) {
      s_packets = { at: Date.now(), list: list };
    }
    cb(null, list);
  });
}

// cb(err, [{id, name, lat, lng, linky:[]}, ...])  (~204 stops, ~46 KB)
// The API wraps the list in a { stations: [...] } envelope (changed 2026-06;
// was a bare array). Unwrap here so callers keep receiving a plain array.
function getStations(packet, cb) {
  request('POST', '/stations', { packet: String(packet) }, function (err, body) {
    if (err) {
      cb(err);
      return;
    }
    var list = body && body.stations;
    if (!Array.isArray(list)) {
      cb({ type: 'parse' });
      return;
    }
    cb(null, list);
  });
}

// datum must be DD_MM_YYYY (underscores!).
// cb(err, [{line, dest, time, delay}, ...]) — normalized, linka trimmed,
// delay in seconds or null when the API has no realtime match.
// Endpoint renamed /odjezd -> /odjezdy and rows wrapped in a
// { departures: [...] } envelope (changed 2026-06).
function getDepartures(packet, stopId, datum, cb) {
  var body = {
    packet: String(packet),
    zastavka: String(stopId),
    datum: datum,
  };
  request('POST', '/odjezdy', body, function (err, payload) {
    if (err) {
      cb(err);
      return;
    }
    var rows = payload && payload.departures;
    if (!Array.isArray(rows)) {
      cb({ type: 'parse' });
      return;
    }
    var items = [];
    for (var i = 0; i < rows.length; i++) {
      var r = rows[i];
      if (!r || typeof r.linka !== 'string' || typeof r.odjezd !== 'string') {
        continue; // parse defensively — API is undocumented
      }
      items.push({
        line: r.linka.trim(),
        dest: typeof r.smer === 'string' ? r.smer : '',
        time: r.odjezd,
        delay: typeof r.delay_seconds === 'number' ? r.delay_seconds : null,
      });
    }
    cb(null, items);
  });
}

// Ordered stop list for one line + direction. `smer` is a direction code
// "0"/"1" (NOT the destination text); the API returns stops in travel order.
// cb(err, [{order, name}, ...]) — order is an int; there are no per-stop times.
function getRoute(packet, linka, smer, cb) {
  var body = {
    packet: String(packet),
    linka: String(linka),
    smer: String(smer),
  };
  request('POST', '/trasa', body, function (err, rows) {
    if (err) {
      cb(err);
      return;
    }
    if (!Array.isArray(rows)) {
      cb({ type: 'parse' });
      return;
    }
    var items = [];
    for (var i = 0; i < rows.length; i++) {
      var r = rows[i];
      if (!r || typeof r.name !== 'string') {
        continue; // parse defensively — API is undocumented
      }
      items.push({ order: parseInt(r.order, 10), name: r.name });
    }
    cb(null, items);
  });
}

module.exports = {
  getPackets: getPackets,
  getStations: getStations,
  getDepartures: getDepartures,
  getRoute: getRoute,
};
