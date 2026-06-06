// Builds the Clay config page: 6 favorite-stop text inputs with a shared
// stop-name datalist (injected by custom-clay.js from meta.userData.stops).
// Inputs hold stop NAMES; index.js maps them back to ids on save.

var SLOT_COUNT = 6;

function buildConfig() {
  var items = [
    { type: 'heading', defaultValue: 'MHD HK' },
    {
      type: 'text',
      defaultValue:
        'Vyberte oblíbené zastávky (našeptávač nabídne názvy). ' +
        'Zobrazí se na hodinkách v sekci Oblíbené.',
    },
  ];
  for (var i = 1; i <= SLOT_COUNT; i++) {
    items.push({
      type: 'input',
      messageKey: 'FAV_' + i,
      label: 'Oblíbená ' + i,
      defaultValue: '',
      attributes: { placeholder: 'Název zastávky' },
    });
  }
  items.push({ type: 'submit', defaultValue: 'Uložit' });

  return [{ type: 'section', items: items }];
}

module.exports = {
  buildConfig: buildConfig,
  SLOT_COUNT: SLOT_COUNT,
};
