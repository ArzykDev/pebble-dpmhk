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
var KEY_STATIONS_AT = 'stations:at';
var KEY_FAVORITES = 'cfg:favorites';

// The backend reassigns stop ids WITHIN a weekly packet — it edits the station
// list mid-week (e.g. packet 187 grew 204->205 stops, shifting every id by
// one). A list cached only by packet number therefore goes silently stale and
// resolves favorite names to now-wrong ids (a tap shows a different stop's
// board). So cache the list with a fetch timestamp and refresh on a short time
// budget too: app open forces a refetch (see index.js 'ready'); taps within
// the TTL reuse the cache.
var STATIONS_TTL_MS = 30 * 60 * 1000;

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

// Serve the last cached station list as an offline fallback, or surface err if
// nothing is cached. Keeps favorites resolution / the config page working when
// the network is down.
function serveStationsCacheOr(cb, err) {
  var cached = getJSON(KEY_STATIONS);
  if (cached && cached.length) {
    cb(null, cached);
  } else {
    cb(err);
  }
}

// cb(err, stations[, forceFresh]) — refetches /stations when the weekly packet
// changed OR the cached list is older than STATIONS_TTL_MS (ids drift mid-week);
// forceFresh bypasses the cache entirely (used on app open). Falls back to a
// stale cached list whenever the network is down so resolution keeps working.
function getStations(cb, forceFresh) {
  api.getPackets(function (err, packets) {
    if (err) {
      serveStationsCacheOr(cb, err);
      return;
    }
    var packet = dates.pickPacket(packets, new Date());
    if (!packet) {
      cb({ type: 'no_packet' });
      return;
    }
    var fresh = Date.now() - Number(localStorage.getItem(KEY_STATIONS_AT) || 0)
                  < STATIONS_TTL_MS;
    if (!forceFresh && fresh &&
        localStorage.getItem(KEY_STATIONS_PACKET) === String(packet)) {
      var cached = getJSON(KEY_STATIONS);
      if (cached && cached.length) {
        cb(null, cached);
        return;
      }
    }
    api.getStations(packet, function (err2, stations) {
      if (err2) {
        serveStationsCacheOr(cb, err2); // serve prior list on a transient outage
        return;
      }
      setJSON(KEY_STATIONS, stations);
      localStorage.setItem(KEY_STATIONS_PACKET, String(packet));
      localStorage.setItem(KEY_STATIONS_AT, String(Date.now()));
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
