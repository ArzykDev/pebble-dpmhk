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

Pebble.addEventListener('ready', function () {
  console.log('PKJS ready');
  // Warm the stations cache (no-op when the weekly packet is unchanged)
  cache.getStations(function (err, stations) {
    if (err) {
      console.log('stations warmup failed: ' + JSON.stringify(err));
    } else {
      console.log('stations cached: ' + stations.length);
    }
  });
});

Pebble.addEventListener('appmessage', function (e) {
  var p = e.payload;
  console.log('request op=' + p.OP + ' req=' + p.REQUEST_ID +
              (p.STOP_ID ? ' stop=' + p.STOP_ID : ''));

  if (p.OP === appmsg.OP.GET_DEPARTURES) {
    var stopId = String(p.STOP_ID);
    // Stale-while-revalidate: paint cached board instantly, then live update
    var cached = cache.getDepartures(stopId);
    var painted = !!(cached && cached.items && cached.items.length);
    if (painted) {
      appmsg.sendDepartures(p.REQUEST_ID, null, cached.items, {
        fetchedAt: cached.fetchedAt,
        flags: appmsg.FLAG_CACHED | appmsg.FLAG_STALE,
      });
    }
    departures.fetch(stopId, function (err, items, fetchedAt) {
      if (err) {
        // Keep a painted cached board on screen rather than wiping it with
        // an error — but if nothing was painted, the watch is waiting and
        // MUST get a reply or it stays on "Načítám..." forever
        if (!painted) {
          appmsg.sendDeparturesError(p.REQUEST_ID, err);
        }
        return;
      }
      cache.setDepartures(stopId, items, fetchedAt, Date.now());
      appmsg.sendDepartures(p.REQUEST_ID, null, items, {
        fetchedAt: fetchedAt,
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
    var byName = {};
    (err2 ? [] : stations).forEach(function (s) {
      byName[util.foldName(s.name)] = s;
    });

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
