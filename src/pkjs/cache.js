// Phone-side localStorage cache.
// Keys: stations:packet, stations:list, cfg:favorites
//
// Departure boards are NOT cached here: a board is a snapshot of the next few
// minutes, so a replayed copy shows already-departed buses and stale delays.
// The watch keeps its own last board (persist.c) purely as an offline fallback,
// shown with an explicit Offline badge — the online path always fetches fresh.

var api = require('./api');
var dates = require('./date');

var KEY_STATIONS_PACKET = 'stations:packet';
var KEY_STATIONS = 'stations:list';
var KEY_FAVORITES = 'cfg:favorites';

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
      setJSON(KEY_STATIONS, stations);
      localStorage.setItem(KEY_STATIONS_PACKET, String(packet));
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

module.exports = {
  getStations: getStations,
  getFavorites: getFavorites,
  setFavorites: setFavorites,
};
