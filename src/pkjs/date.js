// Date helpers for the DPMHK API (phone-local time ~ Europe/Prague).

function pad2(n) {
  return n < 10 ? '0' + n : '' + n;
}

// DD_MM_YYYY with underscores — the /odjezd datum format
function toDatum(d) {
  return pad2(d.getDate()) + '_' + pad2(d.getMonth() + 1) + '_' +
         d.getFullYear();
}

function toISO(d) {
  return d.getFullYear() + '-' + pad2(d.getMonth() + 1) + '-' +
         pad2(d.getDate());
}

function hhmm(d) {
  return pad2(d.getHours()) + ':' + pad2(d.getMinutes());
}

// Pick the packet whose [from, to] range covers the date; null if none
function pickPacket(packets, d) {
  var iso = toISO(d);
  for (var i = 0; i < packets.length; i++) {
    var p = packets[i];
    if (p && p.from <= iso && iso <= p.to) {
      return p.packet;
    }
  }
  return null;
}

function nextDay(d) {
  return new Date(d.getTime() + 24 * 60 * 60 * 1000);
}

module.exports = {
  toDatum: toDatum,
  toISO: toISO,
  hhmm: hhmm,
  pickPacket: pickPacket,
  nextDay: nextDay,
};
