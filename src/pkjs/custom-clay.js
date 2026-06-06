// Runs INSIDE the Clay config page webview (serialized — no closures over
// app code). Injects one shared <datalist> with all stop names from
// meta.userData.stops and attaches it to every favorite input.

module.exports = function (minified) {
  var clayConfig = this;

  clayConfig.on(clayConfig.EVENTS.AFTER_BUILD, function () {
    var stops =
      (clayConfig.meta.userData && clayConfig.meta.userData.stops) || [];

    var datalist = document.createElement('datalist');
    datalist.id = 'stops-list';
    for (var i = 0; i < stops.length; i++) {
      var opt = document.createElement('option');
      opt.value = stops[i];
      datalist.appendChild(opt);
    }
    document.body.appendChild(datalist);

    clayConfig.getItemsByType('input').forEach(function (item) {
      item.$manipulatorTarget.set('@list', 'stops-list');
      item.$manipulatorTarget.set('@autocomplete', 'off');
    });
  });
};
