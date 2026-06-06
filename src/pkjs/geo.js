// Nearest stops: phone geolocation + haversine over the cached stations list.

var appmsg = require('./appmsg');
var cache = require('./cache');

var EARTH_R = 6371000;
var GEO_OPTS = {
  enableHighAccuracy: false,
  timeout: 8000,
  maximumAge: 60000,
};

function haversine(lat1, lon1, lat2, lon2) {
  var toRad = Math.PI / 180;
  var dLat = (lat2 - lat1) * toRad;
  var dLon = (lon2 - lon1) * toRad;
  var a = Math.sin(dLat / 2) * Math.sin(dLat / 2) +
          Math.cos(lat1 * toRad) * Math.cos(lat2 * toRad) *
          Math.sin(dLon / 2) * Math.sin(dLon / 2);
  return 2 * EARTH_R * Math.atan2(Math.sqrt(a), Math.sqrt(1 - a));
}

function formatDist(m) {
  return m < 1000 ? Math.round(m) + ' m' : (m / 1000).toFixed(1) + ' km';
}

// cb(errCode, stops[{id, name, dist}])
function findNearest(maxCount, cb) {
  if (!navigator.geolocation) {
    cb(appmsg.ERR.GPS);
    return;
  }
  navigator.geolocation.getCurrentPosition(function (pos) {
    cache.getStations(function (err, stations) {
      if (err) {
        cb(appmsg.errCode(err));
        return;
      }
      var lat = pos.coords.latitude;
      var lon = pos.coords.longitude;
      var nearest = stations
        .map(function (s) {
          return {
            id: String(s.id),
            name: s.name,
            m: haversine(lat, lon, s.lat, s.lng),
          };
        })
        .sort(function (a, b) {
          return a.m - b.m;
        })
        .slice(0, maxCount)
        .map(function (r) {
          return { id: r.id, name: r.name, dist: formatDist(r.m) };
        });
      cb(appmsg.ERR.NONE, nearest);
    });
  }, function () {
    cb(appmsg.ERR.GPS);
  }, GEO_OPTS);
}

module.exports = {
  findNearest: findNearest,
};
