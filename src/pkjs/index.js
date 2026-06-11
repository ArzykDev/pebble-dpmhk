var Clay = require('@rebble/clay');
var appmsg = require('./appmsg');
var cache = require('./cache');
var config = require('./config');
var customClay = require('./custom-clay');
var departures = require('./departures');
var geo = require('./geo');
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

// Re-resolve each favorite's (ephemeral) id by its stable name against the
// current packet's list. Entries whose name vanished are kept untouched.
function remapFavorites(favorites, stations) {
  var byName = indexByFoldedName(stations);
  return favorites.map(function (f) {
    var match = byName[util.foldName(f.name)];
    return match ? { id: String(match.id), name: match.name } : f;
  });
}

// Resolve which stop a departures request should fetch. The watch sends the
// favorite's saved id plus its name; since ids are packet-scoped we trust the
// (stable) name and map it to the current packet's id, so a tap is correct
// even before the favorites mirror has been refreshed. Falls back to the sent
// id when no name travels (older watch build) or the name can't be matched.
function resolveStopId(sentId, stopName, cb) {
  if (!stopName) {
    cb(sentId);
    return;
  }
  cache.getStations(function (err, stations) {
    if (err) {
      cb(sentId);
      return;
    }
    var match = indexByFoldedName(stations)[util.foldName(stopName)];
    cb(match ? String(match.id) : sentId);
  });
}

Pebble.addEventListener('ready', function () {
  console.log('PKJS ready');
  // Warm the stations cache (refetches only when the weekly packet changed)
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
  });
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
    resolveStopId(sentId, stopName, function (liveId) {
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
    var byName = indexByFoldedName(err2 ? [] : stations);

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
