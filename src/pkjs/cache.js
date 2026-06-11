// Phone-side localStorage cache.
// Keys: stations:packet, stations:list, cfg:favorites, dep:index, dep:<stopId>

var api = require('./api');
var dates = require('./date');

var KEY_STATIONS_PACKET = 'stations:packet';
var KEY_STATIONS = 'stations:list';
var KEY_FAVORITES = 'cfg:favorites';
var KEY_DEP_INDEX = 'dep:index'; // list of stopIds with a cached board

function getJSON(key) {
  try {
    return JSON.parse(localStorage.getItem(key));
  } catch (e) {
    return null;
  }
}

function setJSON(key, value) {
  try {
    localStorage.setItem(key, JSON.stringify(value));
  } catch (e) {
    console.log('localStorage write failed: ' + key);
  }
}

// cb(err, stations) — fetches /stations only when the weekly packet changed;
// falls back to a stale cached list when the network is down.
function getStations(cb) {
  api.getPackets(function (err, packets) {
    if (err) {
      var stale = getJSON(KEY_STATIONS);
      if (stale && stale.length) {
        cb(null, stale);
      } else {
        cb(err);
      }
      return;
    }
    var packet = dates.pickPacket(packets, new Date());
    if (!packet) {
      cb({ type: 'no_packet' });
      return;
    }
    if (localStorage.getItem(KEY_STATIONS_PACKET) === String(packet)) {
      var cached = getJSON(KEY_STATIONS);
      if (cached && cached.length) {
        cb(null, cached);
        return;
      }
    }
    api.getStations(packet, function (err2, stations) {
      if (err2) {
        cb(err2);
        return;
      }
      var prevPacket = localStorage.getItem(KEY_STATIONS_PACKET);
      setJSON(KEY_STATIONS, stations);
      localStorage.setItem(KEY_STATIONS_PACKET, String(packet));
      // Departure boards are keyed by the packet-scoped stop id; once the
      // packet changes those ids map to different stops, so drop stale boards
      // to avoid replaying the wrong stop's cached departures.
      if (prevPacket !== null && prevPacket !== String(packet)) {
        clearDepartures();
      }
      cb(null, stations);
    });
  });
}

// favorites: [{id, name}]
function getFavorites() {
  return getJSON(KEY_FAVORITES) || [];
}

function setFavorites(list) {
  setJSON(KEY_FAVORITES, list);
}

// Departure-board cache for stale-while-revalidate.
// {at: epochMs, fetchedAt: "HH:MM", items: [...]} or null
function getDepartures(stopId) {
  return getJSON('dep:' + stopId);
}

function setDepartures(stopId, items, fetchedAt, nowMs) {
  var id = String(stopId);
  setJSON('dep:' + id, { at: nowMs, fetchedAt: fetchedAt, items: items });
  // Track the key so clearDepartures can wipe boards on a packet change
  // (no reliance on localStorage.length/key(), which pypkjs lacks).
  var ids = getJSON(KEY_DEP_INDEX) || [];
  if (ids.indexOf(id) === -1) {
    ids.push(id);
    setJSON(KEY_DEP_INDEX, ids);
  }
}

// Drop every cached departure board; ids are only valid within one packet.
function clearDepartures() {
  var ids = getJSON(KEY_DEP_INDEX) || [];
  try {
    for (var i = 0; i < ids.length; i++) {
      localStorage.removeItem('dep:' + ids[i]);
    }
    localStorage.removeItem(KEY_DEP_INDEX);
  } catch (e) { /* best effort */ }
}

module.exports = {
  getStations: getStations,
  getFavorites: getFavorites,
  setFavorites: setFavorites,
  getDepartures: getDepartures,
  setDepartures: setDepartures,
};
