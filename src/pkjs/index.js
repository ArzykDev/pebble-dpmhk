var Clay = require('@rebble/clay');
var appmsg = require('./appmsg');
var cache = require('./cache');
var config = require('./config');
var customClay = require('./custom-clay');
var departures = require('./departures');
var geo = require('./geo');
var trip = require('./trip');
var util = require('./util');

// Config page is static; the stop-name datalist travels via meta.userData.
// Events handled manually so userData can be filled from the async cache.
var clay = new Clay(config.buildConfig(), customClay, {
  autoHandleEvents: false,
});

function pushFavoritesToWatch(favorites) {
  appmsg.sendStops(0, appmsg.OP.FAVORITES, favorites);
}

// Map folded stop name -> station for the current packet. Stop ids are
// packet-scoped (the weekly /packet reassigns them), so the folded name is the
// stable key used everywhere a stored id must be re-resolved to its live id.
function indexByFoldedName(stations) {
  var byName = {};
  stations.forEach(function (s) {
    byName[util.foldName(s.name)] = s;
  });
  return byName;
}

// Resolve a stop by its stable folded name. Exact match first; if the name
// arrived truncated (the watch caps names at NAME_LEN, so a very long name is
// cut), fall back to a UNIQUE folded-prefix match. Requires a long-ish key and
// uniqueness so it never guesses between two stops. Returns null if unresolved.
// Pass a prebuilt byName index to avoid rebuilding it on repeated lookups.
function resolveStation(stations, name, byName) {
  byName = byName || indexByFoldedName(stations);
  var key = util.foldName(name);
  if (byName[key]) {
    return byName[key];
  }
  if (key.length < 16) {
    return null; // too short to be a safe prefix — treat as not found
  }
  var hit = null;
  for (var i = 0; i < stations.length; i++) {
    if (util.foldName(stations[i].name).indexOf(key) === 0) {
      if (hit) {
        return null; // ambiguous prefix — refuse to guess
      }
      hit = stations[i];
    }
  }
  return hit;
}

// Re-resolve each favorite's (ephemeral) id by its stable name against the
// current packet's list. Entries whose name vanished are kept untouched.
function remapFavorites(favorites, stations) {
  var byName = indexByFoldedName(stations);
  return favorites.map(function (f) {
    var match = resolveStation(stations, f.name, byName);
    return match ? { id: String(match.id), name: match.name } : f;
  });
}

// Resolve which stop a departures request should fetch. cb(err, liveId). The
// watch sends the favorite's saved id plus its name; since ids are packet-scoped
// (and even drift mid-packet) we trust the stable name and map it to the live
// id. When a name IS sent but can't be resolved, we DON'T fall back to the sent
// id — a stale packet-scoped id now points at a different stop, so showing its
// board would be silently wrong; surface an error (-> offline board) instead.
// The sent id is only trusted when no name travels (older watch build) or the
// station list can't be fetched at all (offline best-effort).
function resolveStopId(sentId, stopName, cb) {
  if (!stopName) {
    cb(null, sentId);
    return;
  }
  cache.getStations(function (err, stations) {
    if (err) {
      cb(null, sentId);
      return;
    }
    var match = resolveStation(stations, stopName);
    if (match) {
      cb(null, String(match.id));
    } else {
      cb({ type: 'api' }, null);
    }
  });
}

Pebble.addEventListener('ready', function () {
  console.log('PKJS ready');
  // Force a fresh station list on app open: ids drift mid-packet, so this heals
  // the favorites mirror with current ids every time the app is launched.
  cache.getStations(function (err, stations) {
    if (err) {
      console.log('stations warmup failed: ' + JSON.stringify(err));
      return;
    }
    console.log('stations cached: ' + stations.length);
    // Refresh favorites' (ephemeral) ids and push the corrected mirror so
    // taps always hit the right stop after a weekly packet rollover.
    var refreshed = remapFavorites(cache.getFavorites(), stations);
    cache.setFavorites(refreshed);
    pushFavoritesToWatch(refreshed);
  }, true);
});

Pebble.addEventListener('appmessage', function (e) {
  var p = e.payload;
  console.log('request op=' + p.OP + ' req=' + p.REQUEST_ID +
              (p.STOP_ID ? ' stop=' + p.STOP_ID : '') +
              (p.META_STOP_NAME ? ' name=' + p.META_STOP_NAME : ''));

  if (p.OP === appmsg.OP.GET_DEPARTURES) {
    var sentId = String(p.STOP_ID);
    var stopName = p.META_STOP_NAME ? String(p.META_STOP_NAME) : null;
    // Always fetch fresh — no local replay. A departure board is a snapshot of
    // the next few minutes, so a cached copy is misleading within minutes; the
    // watch shows "Načítám..." until live data lands and falls back to its own
    // persisted board (with the Offline badge) on error (see comm.c).
    // Re-resolve by name so a favorite whose id shifted with the weekly packet
    // still hits the right stop.
    resolveStopId(sentId, stopName, function (rErr, liveId) {
      if (rErr) {
        appmsg.sendDeparturesError(p.REQUEST_ID, appmsg.errCode(rErr));
        return;
      }
      departures.fetch(liveId, function (err, items, fetchedAt) {
        if (err) {
          appmsg.sendDeparturesError(p.REQUEST_ID, err);
          return;
        }
        appmsg.sendDepartures(p.REQUEST_ID, null, items, {
          fetchedAt: fetchedAt,
        });
      });
    });
  } else if (p.OP === appmsg.OP.GET_NEAREST) {
    geo.findNearest(5, function (err, stops) {
      appmsg.sendStops(p.REQUEST_ID, appmsg.OP.GET_NEAREST, stops || [], err);
    });
  } else if (p.OP === appmsg.OP.GET_TRIP) {
    // Watch sends the line, destination text, and current stop name; resolve
    // the /trasa direction by name (no /stations lookup needed).
    trip.fetch(String(p.ROW_LINE), String(p.ROW_DEST),
        p.META_STOP_NAME ? String(p.META_STOP_NAME) : '',
        function (err, names) {
      if (err) {
        appmsg.sendTripError(p.REQUEST_ID, err);
        return;
      }
      appmsg.sendTrip(p.REQUEST_ID, names);
    });
  } else if (p.OP === appmsg.OP.ADD_FAVORITE ||
             p.OP === appmsg.OP.REMOVE_FAVORITE) {
    updateFavorite(p.OP === appmsg.OP.ADD_FAVORITE, String(p.STOP_ID));
  }
});

// Watch long-press: add/remove a favorite, then push the new list back
function updateFavorite(add, stopId) {
  cache.getStations(function (err, stations) {
    var favorites = cache.getFavorites().filter(function (f) {
      return f.id !== stopId;
    });
    if (add && !err) {
      for (var i = 0; i < stations.length; i++) {
        if (String(stations[i].id) === stopId) {
          favorites.push({ id: stopId, name: stations[i].name });
          break;
        }
      }
      // Keep the just-added stop (last); evict the oldest when over the cap
      favorites = favorites.slice(-config.SLOT_COUNT);
    }
    cache.setFavorites(favorites);
    pushFavoritesToWatch(favorites);
  });
}

Pebble.addEventListener('showConfiguration', function () {
  cache.getStations(function (err, stations) {
    var names = err
      ? []
      : stations
          .map(function (s) {
            return s.name;
          })
          .sort(util.compareNames);
    // autoHandleEvents is off, so populate meta manually (incl. userData)
    clay.meta = {
      activeWatchInfo: Pebble.getActiveWatchInfo && Pebble.getActiveWatchInfo(),
      accountToken: Pebble.getAccountToken(),
      watchToken: Pebble.getWatchToken(),
      userData: { stops: names },
    };
    Pebble.openURL(clay.generateUrl());
  });
});

Pebble.addEventListener('webviewclosed', function (e) {
  if (!e || !e.response) {
    return;
  }
  var settings;
  try {
    settings = clay.getSettings(e.response, false);
  } catch (err) {
    console.log('config parse failed: ' + err);
    return;
  }

  cache.getStations(function (err2, stations) {
    if (err2) {
      // Without the station list we can't resolve the config inputs (names) to
      // packet-scoped ids, so every input would fail to match and we'd push an
      // empty list — wiping the user's favorites (phone cache + watch mirror).
      // Leave the saved favorites untouched instead.
      console.log('config save: stations unavailable, keeping favorites');
      return;
    }
    var byName = indexByFoldedName(stations);

    var favorites = [];
    for (var i = 1; i <= config.SLOT_COUNT; i++) {
      var raw = settings['FAV_' + i];
      var name = raw && raw.value !== undefined ? raw.value : raw;
      if (!name) {
        continue;
      }
      var match = byName[util.foldName(String(name).trim())];
      if (match) {
        favorites.push({ id: String(match.id), name: match.name });
      }
    }
    console.log('favorites: ' + JSON.stringify(favorites));
    cache.setFavorites(favorites);
    pushFavoritesToWatch(favorites);
  });
});
