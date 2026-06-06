// Departure-board orchestration: packet lookup → /odjezd → late-night merge.

var api = require('./api');
var dates = require('./date');
var appmsg = require('./appmsg');

var MAX_ROWS = 18; // keep in sync with MAX_DEPARTURES in model.h
var LATE_HOUR = 23;
var LATE_MIN_ROWS = 3;

// cb(errCode, items, fetchedAt)
function fetch(stopId, cb) {
  api.getPackets(function (err, packets) {
    if (err) {
      cb(appmsg.errCode(err));
      return;
    }
    var now = new Date();
    var packet = dates.pickPacket(packets, now);
    if (!packet) {
      cb(appmsg.ERR.NO_PACKET);
      return;
    }

    function done(items) {
      cb(appmsg.ERR.NONE, items.slice(0, MAX_ROWS), dates.hhmm(now));
    }

    api.getDepartures(packet, stopId, dates.toDatum(now),
        function (err2, items) {
      if (err2) {
        cb(appmsg.errCode(err2));
        return;
      }
      // Late night: few departures left today — append tomorrow's first ones
      if (now.getHours() >= LATE_HOUR && items.length < LATE_MIN_ROWS) {
        var tomorrow = dates.nextDay(now);
        var tomorrowPacket = dates.pickPacket(packets, tomorrow);
        if (!tomorrowPacket) {
          done(items);
          return;
        }
        api.getDepartures(tomorrowPacket, stopId, dates.toDatum(tomorrow),
            function (err3, more) {
          done(err3 || !more ? items : items.concat(more));
        });
      } else {
        done(items);
      }
    });
  });
}

module.exports = {
  fetch: fetch,
};
