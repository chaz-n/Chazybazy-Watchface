// Clay settings page. Three groups: when to pay for the second hand, what the
// numerals look like, and the colour of every element on the dial.
//
// The colour defaults here mirror the defaults in prv_settings_set_defaults(),
// so a fresh install and a saved-with-no-changes page produce the same face.
module.exports = [
  {
    type: 'heading',
    defaultValue: "Chazybazy's Watchface",
  },
  {
    type: 'section',
    items: [
      {
        type: 'heading',
        defaultValue: 'Second hand',
      },
      {
        type: 'text',
        defaultValue:
          'A sweeping second hand wakes the watch and redraws the dial every ' +
          'second. Turning it off, or showing it only while the backlight is ' +
          'lit, lets the face sleep between minutes and noticeably extends ' +
          'battery life.',
      },
      {
        type: 'radiogroup',
        messageKey: 'SecondHandMode',
        defaultValue: '0',
        options: [
          { label: 'Always on', value: '0' },
          { label: 'Only when the backlight is on', value: '1' },
          { label: 'Off', value: '2' },
        ],
      },
      {
        type: 'text',
        defaultValue:
          'The hand follows the backlight itself, however the screen was lit ' +
          '-- a wrist flick, a tap, or coming back from the menu -- and ' +
          'dissolves away again when the light goes out.',
      },
    ],
  },
  {
    type: 'section',
    items: [
      {
        type: 'heading',
        defaultValue: 'Numerals',
      },
      {
        type: 'select',
        messageKey: 'NumeralFont',
        defaultValue: '4',
        label: 'Font',
        options: [
          { label: 'Ubuntu Bold - sans', value: '0' },
          { label: 'Source Serif - serif', value: '1' },
          { label: 'Ubuntu Condensed - narrow sans', value: '2' },
          { label: 'Hack Bold - mono', value: '3' },
          { label: 'Gothic - built in', value: '4' },
          { label: 'Bitham - built in', value: '5' },
          { label: 'LECO - built in', value: '6' },
          { label: 'Droid Serif - built in', value: '7' },
        ],
      },
      {
        type: 'select',
        messageKey: 'NumeralSize',
        defaultValue: '1',
        label: 'Size',
        options: [
          { label: 'Small', value: '0' },
          { label: 'Medium', value: '1' },
          { label: 'Large', value: '2' },
        ],
      },
      {
        type: 'text',
        defaultValue:
          'The first four faces ship inside the app and are drawn at a size ' +
          'chosen for each setting. The four marked "built in" come from the ' +
          'watch itself, which only carries a handful of sizes, so those pick ' +
          'the nearest one rather than scaling: Droid Serif has a single size ' +
          'and ignores the setting, and LECO at Large is a tight fit for 11, ' +
          '12 and 1 across the top.',
      },
    ],
  },
  {
    type: 'section',
    items: [
      {
        type: 'heading',
        defaultValue: 'Colours',
      },
      {
        type: 'text',
        defaultValue:
          'The watch shows 64 colours, so a picked colour lands on the ' +
          'nearest one it can display.',
      },
      {
        type: 'color',
        messageKey: 'ColorBackground',
        defaultValue: '0x000000',
        label: 'Background',
        sunlight: false,
      },
      {
        type: 'color',
        messageKey: 'ColorNumerals',
        defaultValue: '0xFFFFFF',
        label: 'Numerals',
        sunlight: false,
      },
      {
        type: 'color',
        messageKey: 'ColorTickMajor',
        defaultValue: '0xAAAAAA',
        label: 'Ticks (5 minute)',
        sunlight: false,
      },
      {
        type: 'color',
        messageKey: 'ColorTickMinor',
        defaultValue: '0x555555',
        label: 'Ticks (minute)',
        sunlight: false,
      },
      {
        type: 'color',
        messageKey: 'ColorHourHand',
        defaultValue: '0xFFFFFF',
        label: 'Hour hand',
        sunlight: false,
      },
      {
        type: 'color',
        messageKey: 'ColorMinuteHand',
        defaultValue: '0xFFFFFF',
        label: 'Minute hand',
        sunlight: false,
      },
      {
        type: 'color',
        messageKey: 'ColorSecondHand',
        defaultValue: '0xFF0000',
        label: 'Second hand',
        sunlight: false,
      },
      {
        type: 'color',
        messageKey: 'ColorCenterCap',
        defaultValue: '0xFF0000',
        label: 'Centre cap',
        sunlight: false,
      },
      {
        type: 'color',
        messageKey: 'ColorDateText',
        defaultValue: '0xFFFFFF',
        label: 'Date text',
        sunlight: false,
      },
      {
        type: 'color',
        messageKey: 'ColorDateFill',
        defaultValue: '0x000000',
        label: 'Date window',
        sunlight: false,
      },
      {
        type: 'color',
        messageKey: 'ColorDateBorder',
        defaultValue: '0x555555',
        label: 'Date window edge',
        sunlight: false,
      },
    ],
  },
  {
    type: 'section',
    items: [
      {
        type: 'heading',
        defaultValue: 'Reset',
      },
      {
        type: 'button',
        id: 'resetButton',
        defaultValue: 'Reset to defaults',
        description:
          'Puts every setting on this page back to the way the face shipped. ' +
          'Nothing is sent to the watch until you press Save.',
      },
    ],
  },
  {
    type: 'submit',
    defaultValue: 'Save',
  },
];
