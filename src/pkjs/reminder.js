// Departure reminder as a local timeline pin: the pin marks the departure in
// the timeline and its reminder shows the system reminder card the lead time
// before. Local pins ignore `actions`, so the card can't open the app.

var cache = require('./cache');
var dates = require('./date');

// "HH:MM" on the phone clock, rolled to tomorrow past the 23:00 merge window
// (same rule as the watch's theme_minutes_until)
function departureDate(hhmm) {
  var parts = hhmm.split(':');
  var now = new Date();
  var d = new Date(now.getTime());
  d.setHours(parseInt(parts[0], 10), parseInt(parts[1], 10), 0, 0);
  return d.getTime() - now.getTime() <= -2 * 60 * 60 * 1000
      ? dates.nextDay(d) : d;
}

function remove() {
  var id = cache.getReminderPin();
  if (id && typeof Pebble.deleteTimelinePin === 'function') {
    Pebble.deleteTimelinePin(id);
  }
  cache.setReminderPin(null);
}

// Replace the pin with one for this departure; no hhmm = just remove it
function place(line, dest, hhmm, stopName) {
  remove();
  if (!hhmm) {
    return;
  }
  if (typeof Pebble.insertTimelinePin !== 'function') {
    console.log('reminder: this Pebble app has no local timeline pins');
    return;
  }
  var dep = departureDate(hhmm);
  var lead = cache.getRemindLead();
  // A deleted pin id can never be reused, so every pin gets a fresh one
  var id = 'odjezd-' + Date.now();
  // No "→": aplite's system font lacks it
  var title = 'Linka ' + line + ', směr ' + dest;
  Pebble.insertTimelinePin({
    id: id,
    time: dep.toISOString(),
    layout: {
      type: 'genericPin',
      title: title,
      // The pin header already shows the time
      body: 'Zastávka ' + stopName,
      tinyIcon: 'system://images/SCHEDULED_EVENT',
    },
    reminders: [
      {
        time: new Date(dep.getTime() - lead * 60 * 1000).toISOString(),
        layout: {
          type: 'genericReminder',
          // The card already says "in N minutes" above the title
          title: 'Linka ' + line,
          locationName: stopName + ', směr ' + dest,
          tinyIcon: 'system://images/NOTIFICATION_REMINDER',
        },
      },
    ],
  });
  cache.setReminderPin(id);
  console.log('reminder: pin ' + id + ' at ' + dep.toISOString());
}

module.exports = {
  place: place,
};
