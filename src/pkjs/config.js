// Clay settings page. The second hand is the one part of this face that costs
// real battery, so the whole page is about when to pay for it.
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
          'second. Turning it off, or showing it only on a wrist flick, lets ' +
          'the face sleep between minutes and noticeably extends battery life.',
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
          'The watch does not report backlight state to a watchface, so ' +
          '"only when the backlight is on" follows the wrist flick that ' +
          'lights the screen: the hand appears on a flick or tap and hides ' +
          'again a few seconds later.',
      },
    ],
  },
  {
    type: 'submit',
    defaultValue: 'Save',
  },
];
