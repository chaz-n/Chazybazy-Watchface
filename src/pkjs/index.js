var Clay = require('pebble-clay');
var clayConfig = require('./config');

// Clay handles showConfiguration/webviewclosed and sends the saved settings to
// the watch itself, so there is nothing else for the phone side to do.
new Clay(clayConfig);
