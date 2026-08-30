var Clay = require('pebble-clay');
var clayConfig = require('./config');

// Runs inside the settings page, not here: Clay serialises this function and
// evaluates it in the page, so it cannot see anything from this file.
//
// The reset button fills the form with the defaults rather than wiping the
// watch directly. Resetting on the watch alone would leave the phone holding
// the old values, ready to send them back on the next save.
function customClay() {
  var clayConfig = this;

  clayConfig.on(clayConfig.EVENTS.AFTER_BUILD, function() {
    var reset = clayConfig.getItemById('resetButton');
    if (!reset) {
      return;
    }

    reset.on('click', function() {
      clayConfig.getAllItems().forEach(function(item) {
        // Headings, descriptions and the buttons themselves hold no setting.
        if (!item.messageKey || item.config.defaultValue === undefined) {
          return;
        }

        var value = item.config.defaultValue;
        // Colours are declared as '0xRRGGBB' strings but set as numbers.
        if (item.config.type === 'color' && typeof value === 'string') {
          value = parseInt(value, 16);
        }
        item.set(value);
      });
    });
  });
}

new Clay(clayConfig, customClay);
